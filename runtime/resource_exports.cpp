#include "iprop/VmInternals.hpp"

#include <algorithm>
#include <limits>
#include <set>

namespace iprop {

void parseResourceExports(std::span<const u8> data, const Section& section, LoadedFile& file) {
    auto reader = sectionReader(data, section);
    const auto count = reader.readU32();
    if (!count || count > kMaxConstPoolEntries)
        throw FormatError("invalid resource export count", section.offset, ErrorReason::InvalidMetadata);
    // Bounds-check before allocating any table, including an oversized count.
    if (static_cast<u64>(count) * 12u + 4u > reader.remaining())
        throw FormatError("truncated resource export table", reader.position(), ErrorReason::TruncatedInput);
    const auto validOwner = [&](u32 owner) { return owner == kRootParent || owner < file.code.parts.size(); };
    std::set<std::pair<u32, std::string>> names;
    for (const auto& output : file.meta.outputs) names.emplace(kRootParent, file.pool.strings[output.nameIndex]);
    for (u32 p = 0; p < file.code.parts.size(); ++p)
        for (const auto& output : file.code.parts[p].outputs) names.emplace(p, file.pool.strings[output.nameIndex]);
    for (u32 i = 0; i < count; ++i) {
        const auto at = reader.position();
        const auto owner = reader.readU32(), name = reader.readU32();
        const auto type = reader.readU16(), reserved = reader.readU16();
        if (!validOwner(owner)) throw FormatError("resource export owner out of range", at, ErrorReason::InvalidMetadata);
        if (name >= file.pool.strings.size() || file.pool.strings[name].empty())
            throw FormatError("resource export name out of range", at + 4, ErrorReason::InvalidMetadata);
        if (type >= file.collections.types.size() ||
            (file.collections.types[type] != ValueType::Mask && file.collections.types[type] != ValueType::Layer))
            throw FormatError("invalid resource export type descriptor", at + 8, ErrorReason::InvalidMetadata);
        if (reserved) throw FormatError("resource export reserved field must be zero", at + 10, ErrorReason::InvalidMetadata);
        if (!names.emplace(owner, file.pool.strings[name]).second)
            throw FormatError("duplicate output name in resource exports", at + 4, ErrorReason::InvalidMetadata);
        file.resourceExports.push_back({owner, name, file.collections.types[type]});
    }
    const auto functions = reader.readU32();
    if (functions != file.code.functions.size())
        throw FormatError("resource export function owner count mismatch", reader.position() - 4, ErrorReason::InvalidMetadata);
    for (u32 f = 0; f < functions; ++f) {
        const auto at = reader.position();
        const auto owner = reader.readU32();
        if (!validOwner(owner)) throw FormatError("function owner out of range", at, ErrorReason::InvalidMetadata);
        file.functionOwners.push_back(owner);
    }
    if (!reader.empty()) throw FormatError("trailing bytes in resource exports", reader.position(), ErrorReason::InvalidMetadata);
}

void validateResourceOwnership(const LoadedFile& file) {
    if (file.resourceExports.empty()) return;
    if (file.functionOwners.size() != file.code.functions.size() || file.functionOwners.front() != kRootParent)
        throw FormatError("invalid entry function owner", ErrorReason::InvalidMetadata, ErrorStage::Validate);
    for (u32 p = 0; p < file.code.parts.size(); ++p) {
        const auto& part = file.code.parts[p];
        if (!part.nameIndex || file.functionOwners[part.functionIndex] != p)
            throw FormatError("resource exports require part instances and matching function owners",
                ErrorReason::InvalidMetadata, ErrorStage::Validate);
    }
    for (u32 f = 0; f < file.code.functions.size(); ++f) {
        const auto& fn = file.code.functions[f];
        const auto owner = file.functionOwners[f];
        for (u32 pc = 0; pc < fn.code.size() / 4; ++pc) {
            const auto ins = decodeInstruction(fn.code.subspan(pc * 4, 4));
            const auto fail = [&](const char* message) {
                throw FormatError(message, ErrorReason::InvalidInstruction, ErrorStage::Validate,
                    ErrorLocation::instruction(fn.fileOffset + pc * 4, f, pc));
            };
            if (ins.opcode == Opcode::Call && file.functionOwners[ins.operand] != owner)
                fail("call crosses resource export asset ownership");
            if (ins.opcode == Opcode::PartDraw && file.code.parts[ins.operand].parent != owner)
                fail("part.draw of a part that is not a child of the drawing asset");
            if (ins.opcode == Opcode::OutMask || ins.opcode == Opcode::OutLayer) {
                if (file.resourceExports[ins.operand].owner != owner)
                    fail("resource export write by a different asset");
            }
            if (ins.opcode == Opcode::PartGetMask || ins.opcode == Opcode::PartGetLayer) {
                const auto part = file.resourceExports[ins.operand].owner;
                if (part == kRootParent || file.code.parts[part].parent != owner)
                    fail("part.get of a resource export that is not a child of the reading asset");
            }
        }
    }
}

namespace detail {
namespace {
SurfaceMapping mapExport(Execution& ex, const SurfaceBuffer& buffer, SurfaceMapping mapping) {
    // A write freezes the complete view in its owner's caller coordinates.
    // A later call with no write never revisits this mapping.
    const auto bounds = validateSurfaceView(ex, buffer, mapping, false);
    if (ex.partCalls.empty()) return mapping;
    const auto& render = ex.partCalls.back().render;
    const auto a = render.transform.mapPixelWide({bounds.x, bounds.y}, render.sourceExtent);
    const auto b = render.transform.mapPixelWide({static_cast<i64>(bounds.x) + bounds.width - 1,
        static_cast<i64>(bounds.y) + bounds.height - 1}, render.sourceExtent);
    const i64 x = std::min(a.x, b.x) + render.targetOrigin.x;
    const i64 y = std::min(a.y, b.y) + render.targetOrigin.y;
    const i64 right = std::max(a.x, b.x) + render.targetOrigin.x;
    const i64 bottom = std::max(a.y, b.y) + render.targetOrigin.y;
    if (x < std::numeric_limits<i32>::min() || y < std::numeric_limits<i32>::min() ||
        right > std::numeric_limits<i32>::max() || bottom > std::numeric_limits<i32>::max())
        ex.trap("resource export mapping is not representable", ErrorReason::NumericRange);
    return {mapping.transform.compose(render.transform), {static_cast<i32>(x), static_cast<i32>(y)}};
}
}

bool stepResourceExports(Execution& ex, Instruction ins) {
    const bool get = ins.opcode == Opcode::PartGetMask || ins.opcode == Opcode::PartGetLayer;
    const bool put = ins.opcode == Opcode::OutMask || ins.opcode == Opcode::OutLayer;
    if (!get && !put) return false;
    if (ins.aux)
        ex.trap("invalid resource export instruction", ErrorReason::InvalidInstruction);
    if (ins.operand >= ex.file.resourceExports.size() || ins.operand >= ex.resourceSlots.size())
        ex.trap("resource export slot index out of range", ErrorReason::IndexRange);
    const auto& declaration = ex.file.resourceExports[ins.operand];
    const auto type = ins.opcode == Opcode::OutMask || ins.opcode == Opcode::PartGetMask
        ? ValueType::Mask : ValueType::Layer;
    if (declaration.type != type) ex.trap("resource export type mismatch", ErrorReason::InvalidOutput);
    const u32 owner = ex.partCalls.empty() ? kRootParent : ex.partCalls.back().partIndex;
    if (get) {
        if (declaration.owner == kRootParent || declaration.owner >= ex.file.code.parts.size() ||
            ex.file.code.parts[declaration.owner].parent != owner)
            ex.trap("resource export is not owned by a direct child", ErrorReason::InvalidOutput);
        if (declaration.owner != ex.frame().lastPart)
            ex.trap("slot does not belong to the last called part", ErrorReason::InvalidOutput);
        const auto& value = ex.resourceSlots[ins.operand];
        const bool assigned = type == ValueType::Mask ? static_cast<bool>(std::get<MaskValue>(value).buffer)
                                                     : static_cast<bool>(std::get<LayerValue>(value).buffer);
        if (!assigned) ex.trap("part resource export not written", ErrorReason::PartOutputMissing);
        ex.push(value);
    } else {
        if (declaration.owner != owner) ex.trap("resource export write by a different asset", ErrorReason::InvalidOutput);
        if (type == ValueType::Mask) {
            auto value = ex.popMask(); value.mapping = mapExport(ex, *value.buffer, value.mapping);
            ex.resourceSlots[ins.operand] = std::move(value);
        } else {
            auto value = ex.popLayer(); value.mapping = mapExport(ex, *value.buffer, value.mapping);
            ex.resourceSlots[ins.operand] = std::move(value);
        }
    }
    ++ex.frame().ip;
    return true;
}
} // namespace detail
} // namespace iprop
