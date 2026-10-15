#pragma once

// The constant pools of a compiled file (spec §5): strings, i32 bit
// patterns, f32 bit patterns and colours, plus the type descriptors and
// collection constants, deduplicated in insertion order so identical inputs
// produce byte-identical outputs.

#include <algorithm>
#include <cstdint>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

#include "iprop/Collections.hpp"
#include "iprop/Constants.hpp"
#include "iprop/Types.hpp"

namespace iprop::compiler {

// The program needs more pool space than a file format holds. Inconsistent
// pool input is a compiler bug instead and throws std::logic_error.
class PoolLimit : public std::length_error {
public:
    explicit PoolLimit(const std::string& message) : std::length_error(message) {}
};

// One deduplicated pool: values in first-insertion order, keyed by their
// encoding and bounded by the u16 index space of the format. The index only
// answers lookups, so its unordered layout never affects pool order.
template<class T, class Key>
class InternedPool {
public:
    explicit InternedPool(const char* kind) : kind_(kind) {}

    // The index of `key`, storing `value` on its first use. `admit` runs
    // only for a new entry, before it is stored, and may throw PoolLimit.
    template<class Admit>
    u32 add(const Key& key, T value, Admit admit) {
        if (const auto found = index_.find(key); found != index_.end()) return found->second;
        if (values_.size() >= kMaxConstPoolEntries) throw PoolLimit(std::string("too many ") + kind_ + " constants");
        admit();
        const auto index = static_cast<u32>(values_.size());
        values_.push_back(std::move(value));
        index_.emplace(key, index);
        return index;
    }
    u32 add(const Key& key, T value) { return add(key, std::move(value), [] {}); }

    const std::vector<T>& values() const { return values_; }
    u32 indexOf(const Key& key) const { return index_.at(key); }

private:
    const char* kind_;
    std::vector<T> values_;
    std::unordered_map<Key, u32> index_;
};

class Pools {
public:
    // Registers a name/literal, returning its pool index. Repeated values
    // return the first index.
    u32 addString(const std::string& text) { return strings.add(text, text); }
    u32 addInt(u32 bits) { return ints.add(bits, bits); }
    u32 addFloat(u32 bits) { return floats.add(bits, bits); }
    u32 addColor(Color color) {
        const u32 key = static_cast<u32>(color.r) |
                        (static_cast<u32>(color.g) << 8) |
                        (static_cast<u32>(color.b) << 16) |
                        (static_cast<u32>(color.a) << 24);
        return colors.add(key, color);
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
        if (!list.data || list.data->elements.size() > kMaxListElements) throw std::logic_error("invalid collection constant");
        // Repeated reads of one shared list (a palette name) skip re-encoding.
        if (const auto found = listSources.find(list.data); found != listSources.end()) return found->second;
        ListValue canonical = list;
        if (list.type.item().isDescription() || list.type.item() == ValueType::Distribution) {
            auto data = std::make_shared<ListData>(*list.data);
            for (auto& value : data->elements) {
                if (auto* d = std::get_if<Distribution>(&value)) { if (d->points) value = distributions.values().at(addDistribution(*d)); }
                else if (const auto* sampler = std::get_if<SamplerValue>(&value)) value = samplers.values().at(addSampler(*sampler));
                else value = steppers.values().at(addStepper(std::get<StepperValue>(value)));
            }
            canonical.data = std::move(data);
        }
        Writer encoding;
        encoding.writeU8(static_cast<u8>(list.type.element));
        if (list.type.item().isDescription()) encoding.writeU8(static_cast<u8>(list.type.parameter));
        writeListElements(encoding, *canonical.data, samplers.values(), distributions.values(), steppers.values());
        const auto index = static_cast<u16>(lists.add(bytes(encoding), std::move(canonical), [&] {
            charge(ResourceArena::listBytes(list.data->elements.size()));
            addType(list.type);
        }));
        listSources.emplace(list.data, index);
        return index;
    }
    u16 addSampler(const SamplerValue& sampler) { return addDescription(sampler, samplers); }
    u16 addStepper(const StepperValue& stepper) { return addDescription(stepper, steppers); }
    u16 addDistribution(const Distribution& d) {
        if (!d.points || d.family != d.points->family) throw std::logic_error("invalid point distribution constant");
        Writer encoding;
        writePointDistribution(encoding, *d.points);
        return static_cast<u16>(distributions.add(bytes(encoding), d, [&] {
            charge(ResourceArena::distributionBytes(d.points->points.size()));
        }));
    }

    const std::vector<TypeDescriptor>& typesValue() const { return types; }
    const std::vector<ListValue>& listsValue() const { return lists.values(); }
    const std::vector<SamplerValue>& samplersValue() const { return samplers.values(); }
    const std::vector<StepperValue>& steppersValue() const { return steppers.values(); }
    const std::vector<Distribution>& distributionsValue() const { return distributions.values(); }

    // The pool contents, in registration order.
    const std::vector<std::string>& stringsValue() const { return strings.values(); }
    const std::vector<u32>& intsValue() const { return ints.values(); }
    const std::vector<u32>& floatsValue() const { return floats.values(); }
    const std::vector<Color>& colorsValue() const { return colors.values(); }

    // The registered index of a previously added string (names are
    // registered during planning, before emission).
    u32 indexOf(const std::string& text) const { return strings.indexOf(text); }
    // The index of a registered type descriptor.
    u16 typeIndexOf(TypeDescriptor type) const { return typeDescriptorIndex(type, types); }

private:
    // A wire encoding as an interning key.
    static std::string bytes(const Writer& encoding) {
        const auto& data = encoding.data();
        return {data.begin(), data.end()};
    }
    // Samplers and steppers share one encoding, each in its own pool.
    template<class Handle>
    u16 addDescription(const Handle& description, InternedPool<Handle, std::string>& pool) {
        if (!description.data || description.data->type != description.type)
            throw std::logic_error("invalid sampler or stepper constant");
        Writer encoding;
        encoding.writeU8(static_cast<u8>(description.type.element));
        Handle canonical = description;
        if (description.data->distribution.points) {
            auto data = std::make_shared<CapturedRangeData>(*description.data);
            data->distribution = distributions.values().at(addDistribution(data->distribution));
            canonical.data = std::move(data);
        }
        writeDescriptionData(encoding, *canonical.data, distributions.values());
        return static_cast<u16>(pool.add(bytes(encoding), std::move(canonical), [&] {
            charge(ResourceArena::descriptionBytes());
            addType(description.type);
        }));
    }
    // Collection constants share the runtime's generation data budget.
    void charge(u64 bytes) {
        if (bytes > kMaxGenerationData - collectionBytes) throw PoolLimit("collection pool exceeds 64 MiB");
        collectionBytes += bytes;
    }

    u64 collectionBytes = 0;
    std::vector<TypeDescriptor> types;
    InternedPool<ListValue, std::string> lists{"collection"};
    std::map<std::shared_ptr<const ListData>, u16> listSources;
    InternedPool<SamplerValue, std::string> samplers{"sampler"};
    InternedPool<StepperValue, std::string> steppers{"stepper"};
    InternedPool<Distribution, std::string> distributions{"distribution"};
    InternedPool<std::string, std::string> strings{"string"};
    InternedPool<u32, u32> ints{"integer"};
    InternedPool<u32, u32> floats{"float"};
    InternedPool<Color, u32> colors{"color"};
};

} // namespace iprop::compiler
