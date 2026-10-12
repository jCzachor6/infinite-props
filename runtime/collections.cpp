#include "iprop/Collections.hpp"
#include "iprop/math/Distribution.hpp"
#include <algorithm>

namespace iprop {

TypeDescriptor valueType(const Value& value) {
    if (const auto* list = std::get_if<ListValue>(&value)) return list->type;
    if (const auto* builder = std::get_if<ListBuilder>(&value))
        return {TypeDescriptor::Builder, builder->data ? builder->data->list.type.element : TypeDescriptor::Kind::Void};
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
        const auto reserved = reader.readU16();
        if (kind != static_cast<u8>(TypeDescriptor::List) || element > static_cast<u8>(TypeDescriptor::Distribution) || reserved != 0)
            throw FormatError("invalid list type descriptor", at, ErrorReason::InvalidCode);
        TypeDescriptor type{TypeDescriptor::List, static_cast<TypeDescriptor::Kind>(element)};
        if (std::find(types.begin(), types.end(), type) != types.end())
            throw FormatError("duplicate type descriptor", at, ErrorReason::InvalidCode);
        types.push_back(type);
    }
    if (!reader.empty()) throw FormatError("trailing bytes in type descriptors", reader.position(), ErrorReason::InvalidCode);
    return types;
}

std::vector<ListValue> parseCollectionPool(std::span<const u8> data, const Section& section,
                                         std::span<const TypeDescriptor> types) {
    auto reader = sectionReader(data, section);
    const auto count = reader.readU32();
    if (count > kMaxConstPoolEntries) throw FormatError("too many collection constants", section.offset, ErrorReason::InvalidConstants);
    std::vector<ListValue> lists;
    u64 total = 0;
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
                Distribution distribution;
                distribution.family = static_cast<DistributionFamily>(reader.readU8());
                for (auto& parameter : distribution.parameters) parameter = reader.readF32();
                // Pool constructors have exactly the same validity as runtime constructors.
                if (!math::validDistribution(distribution))
                    throw FormatError("invalid collection distribution", reader.position() - 17, ErrorReason::InvalidConstants);
                list->elements.emplace_back(distribution);
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

void writeListElements(Writer& writer, const ListData& list) {
    for (const auto& value : list.elements) {
        if (valueType(value) != list.type.item()) throw std::logic_error("collection element type mismatch");
        if (const auto* i = std::get_if<i32>(&value)) writer.writeI32(*i);
        else if (const auto* f = std::get_if<f32>(&value)) writer.writeF32(*f);
        else if (const auto* c = std::get_if<Color>(&value)) writer.writeColor(*c);
        else if (const auto* v = std::get_if<Vec2>(&value)) { writer.writeF32(v->x); writer.writeF32(v->y); }
        else if (const auto* d = std::get_if<Distribution>(&value)) {
            writer.writeU8(static_cast<u8>(d->family));
            for (const auto p : d->parameters) writer.writeF32(p);
        }
        else throw std::logic_error("unsupported collection element");
    }
}
} // namespace iprop
