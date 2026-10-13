#include "iprop/Collections.hpp"
#include "iprop/VmInternals.hpp"
#include "iprop/math/Distribution.hpp"
#include <algorithm>

namespace iprop {
namespace {
DistributionNumber readNumber(Reader& reader, u8 type) {
    if (type == 0) return reader.readI32();
    if (type == 1) return reader.readF32();
    throw FormatError("invalid distribution point numeric type", reader.position(), ErrorReason::InvalidConstants);
}
void writeNumber(Writer& writer, const DistributionNumber& number) {
    if (const auto* i = std::get_if<i32>(&number)) writer.writeI32(*i);
    else writer.writeF32(std::get<f32>(number));
}
}

void writePointDistribution(Writer& writer, const PointDistributionData& data) {
    if (const auto* error = math::pointDistributionError(data)) throw std::logic_error(error);
    writer.writeU8(static_cast<u8>(data.family)); writer.writeU8(0);
    writer.writeU16(static_cast<u16>(data.points.size()));
    for (const auto& point : data.points) {
        writer.writeU8(static_cast<u8>(point.key.index()));
        writer.writeU8(static_cast<u8>(point.value.index())); writer.writeU16(0);
        writeNumber(writer, point.key); writeNumber(writer, point.value);
    }
}

std::vector<Distribution> parseDistributionPool(std::span<const u8> bytes, const Section& section) {
    auto reader = sectionReader(bytes, section);
    const auto count = reader.readU32();
    if (count > kMaxConstPoolEntries) throw FormatError("too many distribution constants", section.offset, ErrorReason::InvalidConstants);
    std::vector<Distribution> result;
    u64 total = 0;
    for (u32 i = 0; i < count; ++i) {
        const auto at = reader.position();
        const auto family = static_cast<DistributionFamily>(reader.readU8());
        const auto reserved = reader.readU8();
        const auto size = reader.readU16();
        if (!math::pointDistributionSpec(family) || reserved)
            throw FormatError("invalid point distribution family or reserved byte", at, ErrorReason::InvalidConstants);
        if (const auto* error = math::pointDistributionCountError(family,size))
            throw FormatError(error, at + 2, ErrorReason::InvalidConstants);
        total += ResourceArena::distributionBytes(size);
        if (total > kMaxGenerationData) throw FormatError("collection pool exceeds 64 MiB", at, ErrorReason::InvalidConstants);
        auto data = std::make_shared<PointDistributionData>(); data->family = family;
        data->points.reserve(size);
        for (u16 n = 0; n < size; ++n) {
            const auto keyType = reader.readU8(), valueType = reader.readU8();
            if (reader.readU16()) throw FormatError("distribution point reserved field must be zero", reader.position() - 2, ErrorReason::InvalidConstants);
            data->points.push_back({readNumber(reader, keyType), readNumber(reader, valueType)});
        }
        if (const auto* error = math::pointDistributionError(*data)) throw FormatError(error, at, ErrorReason::InvalidConstants);
        Distribution distribution; distribution.family = family; distribution.points = std::move(data);
        result.push_back(std::move(distribution));
    }
    if (!reader.empty()) throw FormatError("trailing bytes in distribution constants", reader.position(), ErrorReason::InvalidConstants);
    return result;
}

Distribution readDistributionData(Reader& reader, std::span<const Distribution> pool) {
    const auto at = reader.position();
    const auto family = static_cast<DistributionFamily>(reader.readU8());
    if (math::pointDistributionSpec(family)) {
        const auto index = reader.readU16();
        if (index >= pool.size() || pool[index].family != family)
            throw FormatError("distribution constant reference out of range or family mismatch", at, ErrorReason::InvalidConstants);
        return pool[index];
    }
    Distribution d; d.family = family;
    for (auto& p : d.parameters) p = reader.readF32();
    if (!math::validDistribution(d)) throw FormatError("invalid constant distribution", at, ErrorReason::InvalidConstants);
    return d;
}

void writeDistributionData(Writer& writer, const Distribution& d, std::span<const Distribution> pool) {
    writer.writeU8(static_cast<u8>(d.family));
    if (d.points) {
        const auto found = std::find_if(pool.begin(), pool.end(), [&](const auto& candidate) { return candidate.points == d.points && candidate.family == d.family; });
        if (found == pool.end()) throw std::logic_error("missing point distribution constant");
        writer.writeU16(static_cast<u16>(found - pool.begin()));
    } else for (const auto p : d.parameters) writer.writeF32(p);
}
} // namespace iprop

namespace iprop::detail {
bool stepPointDistributions(Execution& ex, Instruction ins) {
    const auto builder = [&]() {
        const auto value = ex.pop();
        const auto* b = std::get_if<PointDistributionBuilder>(&value);
        if (!b || !b->data) ex.trap("invalid distribution builder", ErrorReason::StackType);
        return b->data;
    };
    switch (ins.opcode) {
    case Opcode::ConstDistribution: {
        if (ins.operand >= ex.file.collections.distributions.size()) ex.trap("distribution constant index out of range", ErrorReason::IndexRange);
        const auto& d = ex.file.collections.distributions[ins.operand];
        if (!ex.resources.distributionConstants.contains(d.points.get())) {
            ex.spend(d.points->points.size());
            ex.allocateResource(ResourceArena::distributionBytes(d.points->points.size()));
            ex.resources.distributionConstants.insert(d.points.get());
        }
        ex.push(d); break;
    }
    case Opcode::DistributionBegin: {
        const auto count = ex.popI32();
        const auto family = static_cast<DistributionFamily>(ins.operand);
        if (ins.operand > 255 || !math::pointDistributionSpec(family)) ex.trap("unknown point distribution family", ErrorReason::NumericRange);
        if (const auto* error = math::pointDistributionCountError(family,count < 0 ? 0 : static_cast<std::size_t>(count)))
            ex.trap(error, ErrorReason::NumericRange);
        ex.spend(static_cast<u64>(count));
        ex.allocateResource(ResourceArena::distributionBytes(static_cast<std::size_t>(count)));
        auto data = std::make_shared<PointDistributionBuilderData>();
        data->distribution.family = static_cast<DistributionFamily>(ins.operand);
        data->expected = static_cast<std::size_t>(count);
        data->distribution.points.reserve(data->expected);
        ex.push(PointDistributionBuilder{std::move(data)}); break;
    }
    case Opcode::DistributionAppend: {
        if (ins.operand > 3) ex.trap("invalid distribution point types", ErrorReason::StackType);
        DistributionPoint point;
        point.value = (ins.operand & 2) ? DistributionNumber(ex.popF32()) : DistributionNumber(ex.popI32());
        point.key = (ins.operand & 1) ? DistributionNumber(ex.popF32()) : DistributionNumber(ex.popI32());
        auto data = builder();
        auto& points = data->distribution.points;
        if (points.size() >= data->expected) ex.trap("too many distribution points", ErrorReason::IndexRange);
        const auto* previous = points.empty() ? nullptr : &points.back();
        if (const auto* error = math::pointDistributionEntryError(point, previous, data->distribution.family)) ex.trap(error, ErrorReason::NumericRange);
        data->mass = data->mass + math::pointDistributionIncrement(data->distribution.family, point, previous);
        points.push_back(std::move(point));
        ex.push(PointDistributionBuilder{std::move(data)}); break;
    }
    case Opcode::DistributionEnd: {
        if (ins.operand) ex.trap("distribution.end operand must be zero", ErrorReason::NumericRange);
        auto data = builder();
        if (data->distribution.points.size() != data->expected) ex.trap("incomplete distribution builder", ErrorReason::IndexRange);
        if (!(data->mass > 0.0)) ex.trap(data->distribution.family == DistributionFamily::Density ?
            "density needs positive total area" : "weighted needs positive total mass", ErrorReason::NumericRange);
        std::shared_ptr<const PointDistributionData> frozen(data, &data->distribution);
        ex.resources.distributions.push_back(frozen);
        Distribution d; d.family = frozen->family; d.points = std::move(frozen);
        ex.push(std::move(d)); break;
    }
    default: return false;
    }
    ++ex.frame().ip;
    return true;
}
} // namespace iprop::detail
