#include "iprop/Collections.hpp"
#include "iprop/math/Distribution.hpp"
#include "iprop/Samplers.hpp"
#include <algorithm>
#include <string>
#include <type_traits>

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

namespace {
template<class Handle>
std::vector<Handle> parseDescriptionPool(std::span<const u8> data, const Section& section,
                                         const CollectionPool& pools, PoolBudget& budget) {
    constexpr bool stepper = std::is_same_v<Handle, StepperValue>;
    const std::string name = stepper ? "stepper" : "sampler";
    auto reader = sectionReader(data, section);
    const auto count = reader.readU32();
    if (count > kMaxConstPoolEntries)
        throw FormatError("too many " + name + " constants", section.offset, ErrorReason::InvalidConstants);
    std::vector<Handle> descriptions;
    for (u32 i = 0; i < count; ++i) {
        const auto at = reader.position();
        budget.charge(ResourceArena::descriptionBytes(), at);
        const auto index = reader.readU16();
        const auto mode = reader.readU8();
        const auto reserved = reader.readU8();
        if (index >= pools.types.size() || pools.types[index].isStepper() != stepper || !pools.types[index].isDescription())
            throw FormatError(name + " constant type descriptor out of range", at, ErrorReason::InvalidConstants);
        if (mode > static_cast<u8>(SamplerMode::With) || reserved)
            throw FormatError("invalid " + name + " constant mode or reserved byte", at + 2, ErrorReason::InvalidConstants);
        auto description = std::make_shared<CapturedRangeData>();
        description->type = pools.types[index]; description->mode = static_cast<SamplerMode>(mode);
        description->lower = readSamplerBound(reader, description->type.item());
        if (description->mode != SamplerMode::Constant) description->upper = readSamplerBound(reader, description->type.item());
        if (description->mode == SamplerMode::With) description->distribution = readDistributionData(reader, pools.distributions);
        if (const auto* error = samplerBoundsError(description->type.item(), description->lower, description->upper,
                                                 description->mode != SamplerMode::Constant))
            throw FormatError(error, at, ErrorReason::InvalidConstants);
        descriptions.push_back({description->type, std::move(description)});
    }
    if (!reader.empty()) throw FormatError("trailing bytes in " + name + " constants", reader.position(), ErrorReason::InvalidConstants);
    return descriptions;
}

// The pool index of a description element, found by type and shared data.
template<class Handle>
u16 descriptionIndex(std::span<const Handle> pool, const Handle& handle) {
    const auto found = std::find_if(pool.begin(), pool.end(), [&](const auto& candidate) {
        return candidate.type == handle.type && candidate.data == handle.data;
    });
    if (found == pool.end()) throw std::logic_error("missing collection description constant");
    return static_cast<u16>(found - pool.begin());
}

} // namespace

void PoolBudget::charge(u64 size, std::size_t at) {
    if (size > kMaxGenerationData - bytes) throw FormatError("collection pool exceeds 64 MiB", at, ErrorReason::InvalidConstants);
    bytes += size;
}

std::vector<SamplerValue> parseSamplerPool(std::span<const u8> data, const Section& section,
                                          const CollectionPool& pools, PoolBudget& budget) {
    return parseDescriptionPool<SamplerValue>(data, section, pools, budget);
}
std::vector<StepperValue> parseStepperPool(std::span<const u8> data, const Section& section,
                                          const CollectionPool& pools, PoolBudget& budget) {
    return parseDescriptionPool<StepperValue>(data, section, pools, budget);
}
void writeDescriptionData(Writer& writer, const CapturedRangeData& description, std::span<const Distribution> distributions) {
    if (!description.type.isDescription() || samplerBoundsError(description.type.item(), description.lower, description.upper,
        description.mode != SamplerMode::Constant) || static_cast<u8>(description.mode) > static_cast<u8>(SamplerMode::With) ||
        (description.mode == SamplerMode::With && !math::validDistribution(description.distribution)))
        throw std::logic_error("invalid description constant");
    writer.writeU8(static_cast<u8>(description.mode)); writer.writeU8(0);
    writeSamplerBound(writer, description.lower);
    if (description.mode != SamplerMode::Constant) writeSamplerBound(writer, description.upper);
    if (description.mode == SamplerMode::With) writeDistributionData(writer, description.distribution, distributions);
}

const CapturedRangeData* descriptionData(const Value& value) {
    if (const auto* sampler = std::get_if<SamplerValue>(&value)) return sampler->data.get();
    if (const auto* stepper = std::get_if<StepperValue>(&value)) return stepper->data.get();
    return nullptr;
}

TypeDescriptor valueType(const Value& value) {
    if (std::holds_alternative<MaskValue>(value)) return TypeDescriptor::Mask;
    if (std::holds_alternative<LayerValue>(value)) return TypeDescriptor::Layer;
    if (std::holds_alternative<PointDistributionBuilder>(value)) return TypeDescriptor::PointBuilder;
    if (const auto* sampler = std::get_if<SamplerValue>(&value)) return sampler->type;
    if (const auto* stepper = std::get_if<StepperValue>(&value)) return stepper->type;
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

u16 typeDescriptorIndex(TypeDescriptor type, std::span<const TypeDescriptor> types) {
    const auto found = std::find(types.begin(), types.end(), type);
    if (found == types.end()) throw std::logic_error("missing type descriptor");
    return static_cast<u16>(found - types.begin());
}

void writeTypeReference(Writer& writer, TypeDescriptor type, std::span<const TypeDescriptor> types) {
    if (type.isScalar()) { writer.writeU8(static_cast<u8>(type)); return; }
    const auto index = typeDescriptorIndex(type, types);
    writer.writeU8(kDescriptorReference);
    writer.writeU16(index);
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
            // References only point backward to description descriptors, so cycles,
            // list nesting and dangling/forward references cannot enter the VM.
            if (reference >= types.size() || !types[reference].isDescription())
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
        if (type.isList() && type.item().isDescription()) {
            const auto preceding = types.begin() + static_cast<std::ptrdiff_t>(i);
            const auto found = std::find(types.begin(), preceding, type.item());
            if (found == preceding) throw std::logic_error("missing preceding description descriptor");
            writer.writeU8(kDescriptorReference);
            writer.writeU16(static_cast<u16>(found - types.begin()));
        } else {
            writer.writeU8(static_cast<u8>(type.element)); writer.writeU16(0);
        }
    }
}

std::vector<ListValue> parseCollectionPool(std::span<const u8> data, const Section& section,
                                         const CollectionPool& pools, PoolBudget& budget) {
    auto reader = sectionReader(data, section);
    const auto count = reader.readU32();
    if (count > kMaxConstPoolEntries) throw FormatError("too many collection constants", section.offset, ErrorReason::InvalidConstants);
    std::vector<ListValue> lists;
    // A description element is a u16 index into its own pool, with an exact type match.
    const auto description = [&](const auto& pool, TypeDescriptor type) -> Value {
        const auto reference = reader.readU16();
        if (reference >= pool.size() || pool[reference].type != type)
            throw FormatError(std::string("collection ") + (type.isStepper() ? "stepper" : "sampler") +
                " reference out of range or type mismatch", reader.position() - 2, ErrorReason::InvalidConstants);
        return pool[reference];
    };
    for (u32 i = 0; i < count; ++i) {
        const auto at = reader.position();
        const auto index = reader.readU16();
        const auto length = reader.readU16();
        if (index >= pools.types.size() || !pools.types[index].isList())
            throw FormatError("collection type descriptor out of range", at, ErrorReason::InvalidConstants);
        if (length > kMaxListElements) throw FormatError("list exceeds 256 elements", at, ErrorReason::InvalidConstants);
        budget.charge(ResourceArena::listBytes(length), at);
        auto list = std::make_shared<ListData>();
        list->type = pools.types[index];
        const auto item = list->type.item();
        for (u16 n = 0; n < length; ++n) {
            switch (item.kind) {
            case TypeDescriptor::I32: list->elements.emplace_back(reader.readI32()); break;
            case TypeDescriptor::F32: list->elements.emplace_back(reader.readF32()); break;
            case TypeDescriptor::Color: list->elements.emplace_back(reader.readColor()); break;
            case TypeDescriptor::Vec2: list->elements.emplace_back(Vec2{reader.readF32(), reader.readF32()}); break;
            case TypeDescriptor::Distribution:
                list->elements.emplace_back(readDistributionData(reader, pools.distributions)); break;
            case TypeDescriptor::Sampler: list->elements.push_back(description(pools.samplers, item)); break;
            case TypeDescriptor::Stepper: list->elements.push_back(description(pools.steppers, item)); break;
            default: throw FormatError("invalid collection element type", at, ErrorReason::InvalidConstants);
            }
        }
        lists.push_back({list->type, std::move(list)});
    }
    if (!reader.empty()) throw FormatError("trailing bytes in collections", reader.position(), ErrorReason::InvalidConstants);
    return lists;
}

void writeListElements(Writer& writer, const ListData& list, std::span<const SamplerValue> samplers,
                       std::span<const Distribution> distributions, std::span<const StepperValue> steppers) {
    for (const auto& value : list.elements) {
        if (valueType(value) != list.type.item()) throw std::logic_error("collection element type mismatch");
        if (const auto* i = std::get_if<i32>(&value)) writer.writeI32(*i);
        else if (const auto* f = std::get_if<f32>(&value)) writer.writeF32(*f);
        else if (const auto* c = std::get_if<Color>(&value)) writer.writeColor(*c);
        else if (const auto* v = std::get_if<Vec2>(&value)) { writer.writeF32(v->x); writer.writeF32(v->y); }
        else if (const auto* d = std::get_if<Distribution>(&value)) writeDistributionData(writer, *d, distributions);
        else if (const auto* s = std::get_if<SamplerValue>(&value)) writer.writeU16(descriptionIndex(samplers, *s));
        else if (const auto* t = std::get_if<StepperValue>(&value)) writer.writeU16(descriptionIndex(steppers, *t));
        else throw std::logic_error("unsupported collection element");
    }
}
} // namespace iprop
