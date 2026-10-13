#include "iprop/Collections.hpp"
#include "iprop/math/Distribution.hpp"
#include "iprop/Samplers.hpp"
#include <algorithm>

namespace iprop {
namespace {
Value readSamplerBound(Reader& reader, TypeDescriptor type) {
    switch (type.kind) {
    case TypeDescriptor::I32: return reader.readI32();
    case TypeDescriptor::F32: return reader.readF32();
    case TypeDescriptor::Color: return reader.readColor();
    case TypeDescriptor::Vec2: return Vec2{reader.readF32(), reader.readF32()};
    default: throw std::logic_error("invalid sampler bound type");
    }
}
void writeSamplerBound(Writer& writer, const Value& value) {
    if (const auto* i = std::get_if<i32>(&value)) writer.writeI32(*i);
    else if (const auto* f = std::get_if<f32>(&value)) writer.writeF32(*f);
    else if (const auto* c = std::get_if<Color>(&value)) writer.writeColor(*c);
    else if (const auto* v = std::get_if<Vec2>(&value)) { writer.writeF32(v->x); writer.writeF32(v->y); }
    else throw std::logic_error("invalid sampler bound value");
}
}

std::vector<SamplerValue> parseSamplerPool(std::span<const u8> data, const Section& section,
                                          std::span<const TypeDescriptor> types,
                                          std::span<const Distribution> distributions) {
    auto reader = sectionReader(data, section);
    const auto count = reader.readU32();
    if (count > kMaxConstPoolEntries)
        throw FormatError("too many sampler constants", section.offset, ErrorReason::InvalidConstants);
    std::vector<SamplerValue> samplers;
    for (u32 i = 0; i < count; ++i) {
        const auto at = reader.position();
        const auto index = reader.readU16();
        const auto mode = reader.readU8();
        const auto reserved = reader.readU8();
        if (index >= types.size() || !types[index].isSampler())
            throw FormatError("sampler constant type descriptor out of range", at, ErrorReason::InvalidConstants);
        if (mode > static_cast<u8>(SamplerMode::With) || reserved)
            throw FormatError("invalid sampler constant mode or reserved byte", at + 2, ErrorReason::InvalidConstants);
        auto sampler = std::make_shared<SamplerData>();
        sampler->type = types[index]; sampler->mode = static_cast<SamplerMode>(mode);
        sampler->lower = readSamplerBound(reader, sampler->type.item());
        if (sampler->mode != SamplerMode::Constant) sampler->upper = readSamplerBound(reader, sampler->type.item());
        if (sampler->mode == SamplerMode::With) {
            sampler->distribution = readDistributionData(reader, distributions);
        }
        if (const auto* error = samplerBoundsError(sampler->type.item(), sampler->lower, sampler->upper,
                                                 sampler->mode != SamplerMode::Constant))
            throw FormatError(error, at, ErrorReason::InvalidConstants);
        samplers.push_back({sampler->type, std::move(sampler)});
    }
    if (!reader.empty()) throw FormatError("trailing bytes in sampler constants", reader.position(), ErrorReason::InvalidConstants);
    return samplers;
}

void writeSamplerData(Writer& writer, const SamplerData& sampler, std::span<const Distribution> distributions) {
    if (!sampler.type.isSampler() || samplerBoundsError(sampler.type.item(), sampler.lower, sampler.upper,
        sampler.mode != SamplerMode::Constant) || static_cast<u8>(sampler.mode) > static_cast<u8>(SamplerMode::With) ||
        (sampler.mode == SamplerMode::With && !math::validDistribution(sampler.distribution)))
        throw std::logic_error("invalid sampler constant");
    writer.writeU8(static_cast<u8>(sampler.mode)); writer.writeU8(0);
    writeSamplerBound(writer, sampler.lower);
    if (sampler.mode != SamplerMode::Constant) writeSamplerBound(writer, sampler.upper);
    if (sampler.mode == SamplerMode::With) {
        writeDistributionData(writer, sampler.distribution, distributions);
    }
}

TypeDescriptor valueType(const Value& value) {
    if (std::holds_alternative<LayerValue>(value)) return TypeDescriptor::Layer;
    if (std::holds_alternative<PointDistributionBuilder>(value)) return TypeDescriptor::PointBuilder;
    if (const auto* sampler = std::get_if<SamplerValue>(&value)) return sampler->type;
    if (const auto* list = std::get_if<ListValue>(&value)) return list->type;
    if (const auto* builder = std::get_if<ListBuilder>(&value))
        return builder->data ? TypeDescriptor::builder(builder->data->list.type) : TypeDescriptor::Builder;
    return TypeDescriptor(static_cast<TypeDescriptor::Kind>(value.index()));
}

TypeDescriptor readTypeReference(Reader& reader, std::span<const TypeDescriptor> types) {
    const auto at = reader.position();
    const auto code = reader.readU8();
    if (code <= static_cast<u8>(TypeDescriptor::Distribution)) return TypeDescriptor(static_cast<TypeDescriptor::Kind>(code));
    if (code != kDescriptorReference) throw FormatError("unknown value type", at, ErrorReason::InvalidCode);
    const auto index = reader.readU16();
    if (index >= types.size()) throw FormatError("type descriptor index out of range", at, ErrorReason::InvalidCode);
    return types[index];
}

void writeTypeReference(Writer& writer, TypeDescriptor type, std::span<const TypeDescriptor> types) {
    if (type.isScalar()) { writer.writeU8(static_cast<u8>(type)); return; }
    const auto found = std::find(types.begin(), types.end(), type);
    if (found == types.end()) throw std::logic_error("missing type descriptor");
    writer.writeU8(kDescriptorReference);
    writer.writeU16(static_cast<u16>(found - types.begin()));
}

std::vector<TypeDescriptor> parseTypeDescriptors(std::span<const u8> data, const Section& section) {
    auto reader = sectionReader(data, section);
    const auto count = reader.readU32();
    if (count > kMaxConstPoolEntries) throw FormatError("too many type descriptors", section.offset, ErrorReason::InvalidCode);
    std::vector<TypeDescriptor> types;
    for (u32 i = 0; i < count; ++i) {
        const auto at = reader.position();
        const auto kind = reader.readU8();
        const auto element = reader.readU8();
        const auto reference = reader.readU16();
        TypeDescriptor type;
        if (kind == static_cast<u8>(TypeDescriptor::List) && element == kDescriptorReference) {
            // References only point backward to sampler descriptors, so cycles,
            // list nesting and dangling/forward references cannot enter the VM.
            if (reference >= types.size() || !types[reference].isSampler())
                throw FormatError("invalid list element descriptor reference", at, ErrorReason::InvalidCode);
            type = TypeDescriptor::list(types[reference]);
        } else {
            type = {static_cast<TypeDescriptor::Kind>(kind), static_cast<TypeDescriptor::Kind>(element)};
            if (!type.needsDescriptor() || !type.isStorable() || reference != 0)
                throw FormatError("invalid parameterized type descriptor", at, ErrorReason::InvalidCode);
        }
        if (std::find(types.begin(), types.end(), type) != types.end())
            throw FormatError("duplicate type descriptor", at, ErrorReason::InvalidCode);
        types.push_back(type);
    }
    if (!reader.empty()) throw FormatError("trailing bytes in type descriptors", reader.position(), ErrorReason::InvalidCode);
    return types;
}

void writeTypeDescriptors(Writer& writer, std::span<const TypeDescriptor> types) {
    writer.writeU32(static_cast<u32>(types.size()));
    for (std::size_t i = 0; i < types.size(); ++i) {
        const auto type = types[i];
        if (!type.needsDescriptor() || !type.isStorable()) throw std::logic_error("invalid type descriptor");
        writer.writeU8(static_cast<u8>(type.kind));
        if (type.isList() && type.item().isSampler()) {
            const auto found = std::find(types.begin(), types.begin() + i, type.item());
            if (found == types.begin() + i) throw std::logic_error("missing preceding sampler descriptor");
            writer.writeU8(kDescriptorReference);
            writer.writeU16(static_cast<u16>(found - types.begin()));
        } else {
            writer.writeU8(static_cast<u8>(type.element)); writer.writeU16(0);
        }
    }
}

std::vector<ListValue> parseCollectionPool(std::span<const u8> data, const Section& section,
                                         std::span<const TypeDescriptor> types,
                                         std::span<const SamplerValue> samplers,
                                         std::span<const Distribution> distributions) {
    auto reader = sectionReader(data, section);
    const auto count = reader.readU32();
    if (count > kMaxConstPoolEntries) throw FormatError("too many collection constants", section.offset, ErrorReason::InvalidConstants);
    std::vector<ListValue> lists;
    u64 total = samplers.size() * ResourceArena::samplerBytes();
    for (const auto& d : distributions) total += ResourceArena::distributionBytes(d.points->points.size());
    for (u32 i = 0; i < count; ++i) {
        const auto at = reader.position();
        const auto index = reader.readU16();
        const auto length = reader.readU16();
        if (index >= types.size() || !types[index].isList())
            throw FormatError("collection type descriptor out of range", at, ErrorReason::InvalidConstants);
        if (length > kMaxListElements) throw FormatError("list exceeds 256 elements", at, ErrorReason::InvalidConstants);
        total += ResourceArena::listBytes(length);
        if (total > kMaxGenerationData) throw FormatError("collection pool exceeds 64 MiB", at, ErrorReason::InvalidConstants);
        auto list = std::make_shared<ListData>();
        list->type = types[index];
        for (u16 n = 0; n < length; ++n) {
            switch (list->type.element) {
            case TypeDescriptor::I32: list->elements.emplace_back(reader.readI32()); break;
            case TypeDescriptor::F32: list->elements.emplace_back(reader.readF32()); break;
            case TypeDescriptor::Color: list->elements.emplace_back(reader.readColor()); break;
            case TypeDescriptor::Vec2: list->elements.emplace_back(Vec2{reader.readF32(), reader.readF32()}); break;
            case TypeDescriptor::Distribution: {
                list->elements.emplace_back(readDistributionData(reader, distributions));
                break;
            }
            case TypeDescriptor::Sampler: {
                const auto reference = reader.readU16();
                if (reference >= samplers.size() || samplers[reference].type != list->type.item())
                    throw FormatError("collection sampler reference out of range or type mismatch", reader.position() - 2, ErrorReason::InvalidConstants);
                list->elements.emplace_back(samplers[reference]);
                break;
            }
            default: throw FormatError("invalid collection element type", at, ErrorReason::InvalidConstants);
            }
        }
        lists.push_back({list->type, std::move(list)});
    }
    if (!reader.empty()) throw FormatError("trailing bytes in collections", reader.position(), ErrorReason::InvalidConstants);
    return lists;
}

void writeListElements(Writer& writer, const ListData& list, std::span<const SamplerValue> samplers,
                       std::span<const Distribution> distributions) {
    for (const auto& value : list.elements) {
        if (valueType(value) != list.type.item()) throw std::logic_error("collection element type mismatch");
        if (const auto* i = std::get_if<i32>(&value)) writer.writeI32(*i);
        else if (const auto* f = std::get_if<f32>(&value)) writer.writeF32(*f);
        else if (const auto* c = std::get_if<Color>(&value)) writer.writeColor(*c);
        else if (const auto* v = std::get_if<Vec2>(&value)) { writer.writeF32(v->x); writer.writeF32(v->y); }
        else if (const auto* d = std::get_if<Distribution>(&value)) {
            writeDistributionData(writer, *d, distributions);
        }
        else if (const auto* s = std::get_if<SamplerValue>(&value)) {
            const auto found = std::find_if(samplers.begin(), samplers.end(), [&](const auto& candidate) {
                return candidate.type == s->type && candidate.data == s->data;
            });
            if (found == samplers.end()) throw std::logic_error("missing collection sampler constant");
            writer.writeU16(static_cast<u16>(found - samplers.begin()));
        } else throw std::logic_error("unsupported collection element");
    }
}
} // namespace iprop
