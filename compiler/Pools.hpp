#pragma once

// The constant pools of a compiled file (spec §5): strings, i32 bit
// patterns, f32 bit patterns and colours, deduplicated in insertion order
// so identical inputs produce byte-identical outputs.

#include <cstdint>
#include <map>
#include <string>
#include <stdexcept>
#include "iprop/Constants.hpp"
#include <vector>

#include "iprop/Types.hpp"
#include "iprop/Collections.hpp"
#include <algorithm>

namespace iprop::compiler {

class PoolLimit : public std::length_error {
public:
    explicit PoolLimit(const char* message) : std::length_error(message) {}
};

class Pools {
public:
    // Registers a name/literal, returning its pool index. Repeated values
    // return the first index.
    u32 addString(const std::string& text) {
        const auto found = stringIndex.find(text);
        if (found != stringIndex.end()) {
            return found->second;
        }
        if (strings.size() >= kMaxConstPoolEntries) throw PoolLimit("too many string constants");
        const u32 index = static_cast<u32>(strings.size());
        strings.push_back(text);
        stringIndex.emplace(text, index);
        return index;
    }

    u32 addInt(u32 bits) {
        const auto found = intIndex.find(bits);
        if (found != intIndex.end()) {
            return found->second;
        }
        if (ints.size() >= kMaxConstPoolEntries) throw PoolLimit("too many integer constants");
        const u32 index = static_cast<u32>(ints.size());
        ints.push_back(bits);
        intIndex.emplace(bits, index);
        return index;
    }

    u32 addFloat(u32 bits) {
        const auto found = floatIndex.find(bits);
        if (found != floatIndex.end()) {
            return found->second;
        }
        if (floats.size() >= kMaxConstPoolEntries) throw PoolLimit("too many float constants");
        const u32 index = static_cast<u32>(floats.size());
        floats.push_back(bits);
        floatIndex.emplace(bits, index);
        return index;
    }

    u32 addColor(Color color) {
        const u32 key = static_cast<u32>(color.r) |
                        (static_cast<u32>(color.g) << 8) |
                        (static_cast<u32>(color.b) << 16) |
                        (static_cast<u32>(color.a) << 24);
        const auto found = colorIndex.find(key);
        if (found != colorIndex.end()) {
            return found->second;
        }
        if (colors.size() >= kMaxConstPoolEntries) throw PoolLimit("too many color constants");
        const u32 index = static_cast<u32>(colors.size());
        colors.push_back(color);
        colorIndex.emplace(key, index);
        return index;
    }

    u16 addType(TypeDescriptor type) {
        if (!type.needsDescriptor() || !type.isStorable()) throw std::logic_error("expected value descriptor");
        if (type.isList() && type.item().isDescription()) addType(type.item());
        const auto found = std::find(types.begin(), types.end(), type);
        if (found != types.end()) return static_cast<u16>(found - types.begin());
        if (types.size() >= kMaxConstPoolEntries) throw PoolLimit("too many type descriptors");
        types.push_back(type);
        return static_cast<u16>(types.size() - 1);
    }
    u16 addList(const ListValue& list) {
        if (!list.data || list.data->elements.size() > kMaxListElements) throw PoolLimit("invalid collection constant");
        ListValue canonical = list;
        if (list.type.item().isDescription() || list.type.item() == ValueType::Distribution) {
            auto data = std::make_shared<ListData>(*list.data);
            for (auto& value : data->elements) {
                if (auto* d = std::get_if<Distribution>(&value)) { if (d->points) value = distributions.at(addDistribution(*d)); }
                else if (const auto* sampler = std::get_if<SamplerValue>(&value)) value = samplers.at(addSampler(*sampler));
                else value = steppers.at(addStepper(std::get<StepperValue>(value)));
            }
            canonical.data = std::move(data);
        }
        Writer encoding;
        encoding.writeU8(static_cast<u8>(list.type.element));
        if (list.type.item().isDescription()) encoding.writeU8(static_cast<u8>(list.type.parameter));
        writeListElements(encoding, *canonical.data, samplers, distributions, steppers);
        if (const auto found = listIndex.find(encoding.data()); found != listIndex.end()) return found->second;
        if (lists.size() >= kMaxConstPoolEntries) throw PoolLimit("too many collection constants");
        const auto allocation = ResourceArena::listBytes(list.data->elements.size());
        if (allocation > kMaxGenerationData - collectionBytes) throw PoolLimit("collection pool exceeds 64 MiB");
        collectionBytes += allocation;
        addType(list.type);
        const auto index = static_cast<u16>(lists.size());
        lists.push_back(std::move(canonical));
        listIndex.emplace(encoding.data(), index);
        return index;
    }
    u16 addSampler(const SamplerValue& sampler) { return addDescription(sampler, samplers, samplerIndex); }
    u16 addStepper(const StepperValue& stepper) { return addDescription(stepper, steppers, stepperIndex); }
    template<class Handle>
    u16 addDescription(const Handle& sampler, std::vector<Handle>& descriptions, std::map<std::vector<u8>, u16>& indices) {
        if (!sampler.data || sampler.data->type != sampler.type) throw PoolLimit("invalid sampler constant");
        Writer encoding;
        encoding.writeU8(static_cast<u8>(sampler.type.element));
        Handle canonical = sampler;
        if (sampler.data->distribution.points) {
            auto data = std::make_shared<SamplerData>(*sampler.data);
            data->distribution = distributions.at(addDistribution(data->distribution));
            canonical.data = std::move(data);
        }
        if (sampler.type.isStepper()) writeStepperData(encoding, *canonical.data, distributions);
        else writeSamplerData(encoding, *canonical.data, distributions);
        if (const auto found = indices.find(encoding.data()); found != indices.end()) return found->second;
        if (descriptions.size() >= kMaxConstPoolEntries) throw PoolLimit("too many sampler constants");
        if (ResourceArena::samplerBytes() > kMaxGenerationData - collectionBytes) throw PoolLimit("collection pool exceeds 64 MiB");
        collectionBytes += ResourceArena::samplerBytes();
        addType(sampler.type);
        const auto index = static_cast<u16>(descriptions.size());
        descriptions.push_back(std::move(canonical)); indices.emplace(encoding.data(), index);
        return index;
    }
    u16 addDistribution(const Distribution& d) {
        if (!d.points || d.family != d.points->family) throw PoolLimit("invalid point distribution constant");
        Writer encoding; writePointDistribution(encoding, *d.points);
        if (const auto found = distributionIndex.find(encoding.data()); found != distributionIndex.end()) return found->second;
        if (distributions.size() >= kMaxConstPoolEntries) throw PoolLimit("too many distribution constants");
        const auto bytes = ResourceArena::distributionBytes(d.points->points.size());
        if (bytes > kMaxGenerationData - collectionBytes) throw PoolLimit("collection pool exceeds 64 MiB");
        collectionBytes += bytes;
        const auto index = static_cast<u16>(distributions.size());
        distributions.push_back(d); distributionIndex.emplace(encoding.data(), index);
        return index;
    }
    const std::vector<TypeDescriptor>& typesValue() const { return types; }
    const std::vector<ListValue>& listsValue() const { return lists; }
    const std::vector<SamplerValue>& samplersValue() const { return samplers; }
    const std::vector<StepperValue>& steppersValue() const { return steppers; }
    const std::vector<Distribution>& distributionsValue() const { return distributions; }

    // The pool contents, in registration order.
    const std::vector<std::string>& stringsValue() const { return strings; }
    const std::vector<u32>& intsValue() const { return ints; }
    const std::vector<u32>& floatsValue() const { return floats; }
    const std::vector<Color>& colorsValue() const { return colors; }

    // The registered index of a previously added string (names are
    // registered during planning, before emission).
    u32 indexOf(const std::string& text) const { return stringIndex.at(text); }

private:
    u64 collectionBytes = 0;
    std::vector<TypeDescriptor> types;
    std::vector<ListValue> lists;
    std::map<std::vector<u8>, u16> listIndex;
    std::vector<SamplerValue> samplers;
    std::map<std::vector<u8>, u16> samplerIndex;
    std::vector<StepperValue> steppers;
    std::map<std::vector<u8>, u16> stepperIndex;
    std::vector<Distribution> distributions;
    std::map<std::vector<u8>, u16> distributionIndex;
    std::vector<std::string> strings;
    std::map<std::string, u32> stringIndex;
    std::vector<u32> ints;
    std::map<u32, u32> intIndex;
    std::vector<u32> floats;
    std::map<u32, u32> floatIndex;
    std::vector<Color> colors;
    std::map<u32, u32> colorIndex;
};

} // namespace iprop::compiler
