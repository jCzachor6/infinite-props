#include "iprop/model/Model.hpp"

#include "iprop/Binary.hpp"
#include "iprop/Format.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace iprop::model {
namespace {

[[noreturn]] void invalid(const std::string& message, std::size_t at, ErrorReason reason = ErrorReason::InvalidMetadata) {
    throw FormatError(message, at, reason);
}

void readModelSection(std::span<const u8> data, const Section& section, LoadedModel& model) {
    if (section.size != kModelSectionSize) invalid("model section must be 32 bytes", section.offset);
    Reader reader = sectionReader(data, section);
    for (f32& component : model.size) {
        component = reader.readF32();
        if (!std::isfinite(component) || !(component > 0.f))
            invalid("model size must be finite and positive", reader.position() - 4);
    }
    model.unitsPerMeter = reader.readF32();
    if (!std::isfinite(model.unitsPerMeter) || !(model.unitsPerMeter > 0.f))
        invalid("model scale must be finite and positive", reader.position() - 4);
    if (reader.readU32() != 0) invalid("unknown model flags", reader.position() - 4);
    for (int i = 0; i < 3; ++i)
        if (reader.readU32() != 0) invalid("reserved model field must be zero", reader.position() - 4);
}

void readSkinsSection(std::span<const u8> data, const Section& section, LoadedModel& model) {
    Reader reader = sectionReader(data, section);
    const u32 count = reader.readU32();
    if (count > kMaxSkinPrograms) invalid("too many skin programs", reader.position() - 4);
    struct Entry {
        std::size_t at = 0;
        u32 nameIndex = 0, offset = 0, length = 0;
    };
    std::vector<Entry> entries(count);
    for (Entry& entry : entries) {
        entry.at = reader.position();
        entry.nameIndex = reader.readU32();
        entry.offset = reader.readU32();
        entry.length = reader.readU32();
        if (entry.nameIndex >= model.file.pool.strings.size()) invalid("skin program name outside const string pool", entry.at);
    }
    const u64 tableEnd = 4u + static_cast<u64>(count) * 12u;
    std::vector<u8> covered(section.size, 0);
    for (std::size_t i = 0; i < entries.size(); ++i) {
        const Entry& entry = entries[i];
        const u64 end = static_cast<u64>(entry.offset) + entry.length;
        if (entry.offset % 4 != 0 || entry.offset < tableEnd || end > section.size || entry.length < kHeaderSize)
            invalid("skin program range is invalid", entry.at);
        for (std::size_t j = 0; j < i; ++j) {
            const Entry& other = entries[j];
            const bool same = other.offset == entry.offset && other.length == entry.length;
            const bool apart = static_cast<u64>(other.offset) + other.length <= entry.offset || end <= other.offset;
            if (!same && !apart) invalid("skin program ranges overlap", entry.at);
        }
        std::fill(covered.begin() + entry.offset, covered.begin() + static_cast<std::ptrdiff_t>(end), u8{1});
    }
    for (u64 i = tableEnd; i < section.size; ++i)
        if (!covered[i] && data[section.offset + i] != 0) invalid("stray bytes in the skins section", section.offset + i);

    model.skins.reserve(count);
    for (std::size_t i = 0; i < entries.size(); ++i) {
        const Entry& entry = entries[i];
        const auto bytes = data.subspan(section.offset + entry.offset, entry.length);
        SkinProgram program;
        program.name = model.file.pool.strings[entry.nameIndex];
        // Equal bytes share one range (the writer stores them once).
        program.identity = static_cast<u32>(i);
        for (std::size_t j = 0; j < i; ++j)
            if (entries[j].offset == entry.offset && entries[j].length == entry.length) {
                program.identity = static_cast<u32>(j);
                break;
            }
        try {
            if (parseHeader(bytes).hasCompression()) throw FormatError("an embedded program must not be compressed");
            program.file = loadFile(bytes);
        } catch (const FormatError& error) {
            invalid("skin program '" + program.name + "': " + error.what(), entry.at);
        }
        program.hash = fnv1a64(bytes);
        model.skins.push_back(std::move(program));
    }
}

const std::string& poolName(const LoadedModel& model, u32 index, std::size_t at, const char* what) {
    if (index >= model.file.pool.strings.size()) invalid(std::string(what) + " name outside const string pool", at);
    const std::string& name = model.file.pool.strings[index];
    if (name.empty()) invalid(std::string(what) + " name must not be empty", at);
    return name;
}

// Section 15: the parts, each a contiguous run of functions after the
// model's own, in order and to the end of the function table.
void readPartsSection(std::span<const u8> data, const Section& section, LoadedModel& model) {
    Reader reader = sectionReader(data, section);
    const u32 count = reader.readU32();
    if (count == 0 || count > kMaxModelParts) invalid("model part count must be in 1..256", reader.position() - 4);
    const u64 functions = model.file.code.functions.size();
    model.parts.reserve(count);
    for (u32 i = 0; i < count; ++i) {
        const std::size_t at = reader.position();
        ModelPartDecl part;
        part.name = poolName(model, reader.readU32(), at, "model part");
        part.firstFunction = reader.readU32();
        part.functionCount = reader.readU32();
        part.parent = reader.readU32();
        for (const ModelPartDecl& other : model.parts)
            if (other.name == part.name) invalid("duplicate model part name '" + part.name + "'", at);
        if (part.parent != kRootParent && part.parent >= i) invalid("a model part's parent must precede it", at);
        const u64 expected = i == 0 ? 0u : static_cast<u64>(model.parts.back().firstFunction) + model.parts.back().functionCount;
        if (part.functionCount == 0 || (i == 0 ? part.firstFunction == 0 : part.firstFunction != expected) ||
            static_cast<u64>(part.firstFunction) + part.functionCount > functions)
            invalid("model part functions must follow the model's own, in order", at);
        model.parts.push_back(std::move(part));
    }
    const ModelPartDecl& last = model.parts.back();
    if (static_cast<u64>(last.firstFunction) + last.functionCount != functions)
        invalid("model part functions must reach the end of the function table", section.offset);
    if (reader.position() != section.offset + section.size) invalid("trailing bytes in the model parts section", reader.position());
}

// Section 14: the 3D output slots, numbered in order.
void readOutputsSection(std::span<const u8> data, const Section& section, LoadedModel& model) {
    Reader reader = sectionReader(data, section);
    const u32 count = reader.readU32();
    if (count == 0 || count > kMaxModelOutputs) invalid("model output count must be in 1..256", reader.position() - 4);
    model.outputs.reserve(count);
    for (u32 i = 0; i < count; ++i) {
        const std::size_t at = reader.position();
        ModelOutputDecl output;
        output.name = poolName(model, reader.readU32(), at, "model output");
        const u8 kind = reader.readU8();
        const u8 list = reader.readU8();
        output.owner = reader.readU16();
        if (kind > static_cast<u8>(ModelOutputKind::Box3d)) invalid("unknown model output kind", at + 4);
        if (list > 1) invalid("model output list flag must be 0 or 1", at + 5);
        if (output.owner != kRootOwner && output.owner >= model.parts.size()) invalid("model output owner out of range", at + 6);
        output.kind = static_cast<ModelOutputKind>(kind);
        output.list = list == 1;
        for (const ModelOutputDecl& other : model.outputs)
            if (other.owner == output.owner && other.name == output.name)
                invalid("duplicate model output name '" + output.name + "'", at);
        model.outputs.push_back(std::move(output));
    }
    if (reader.position() != section.offset + section.size) invalid("trailing bytes in the model outputs section", reader.position());
}

} // namespace

u64 fnv1a64(std::span<const u8> bytes, u64 state) {
    for (const u8 byte : bytes) state = (state ^ byte) * 0x100000001b3ull;
    return state;
}

LoadedModel loadModel(std::span<const u8> bytes) {
    LoadedModel model;
    try {
        model.file = loadFile(bytes, kModelDialect);
    } catch (const FormatError& error) {
        if (error.reason != ErrorReason::BadMagic) throw;
        const bool prop = bytes.size() >= 4 && std::equal(kMagic, kMagic + 4, bytes.begin());
        throw FormatError(prop ? "bad magic: this is a 2D .iprop program, not an .iprop3d model"
                               : "bad magic: not an .iprop3d file",
                          error.offset, ErrorReason::BadMagic, ErrorStage::Load, OffsetSpace::Container);
    }
    const auto data = model.file.bytes();
    const auto& layout = model.file.layout;
    const Section* modelSection = nullptr;
    const Section* skinsSection = nullptr;
    const Section* outputsSection = nullptr;
    const Section* partsSection = nullptr;
    for (const Section& section : layout.sections) {
        if (section.id == SectionId::Parts || section.id == SectionId::ResourceExports)
            invalid("a model file has no parts or resource exports section", section.offset, ErrorReason::InvalidLayout);
        if (section.id == kModelSection) modelSection = &section;
        if (section.id == kSkinsSection) skinsSection = &section;
        if (section.id == kOutputsSection) outputsSection = &section;
        if (section.id == kPartsSection) partsSection = &section;
    }
    if (!modelSection || !skinsSection)
        invalid("missing required section (model, skins)", 0, ErrorReason::InvalidLayout);

    const auto& meta = model.file.meta;
    const Section& metaSection = *layout.find(SectionId::Meta);
    if (meta.sizeX != 1 || meta.sizeY != 1 || meta.origin.x != 0.f || meta.origin.y != 0.f || meta.hasEmission ||
        std::signbit(meta.origin.x) || std::signbit(meta.origin.y))
        invalid("a model's meta section has no canvas: size 1x1, origin and flags zero", metaSection.offset);
    if (!meta.outputs.empty()) invalid("a model has no 2D outputs", metaSection.offset);
    if (!model.file.code.parts.empty())
        invalid("a model program has no parts", layout.find(SectionId::Code)->offset, ErrorReason::InvalidCode);

    readModelSection(data, *modelSection, model);
    readSkinsSection(data, *skinsSection, model);
    if (partsSection) readPartsSection(data, *partsSection, model);
    if (outputsSection) readOutputsSection(data, *outputsSection, model);
    return model;
}

void writeOutputsSection(Writer& out, std::span<const OutputEntry> entries) {
    out.writeU32(static_cast<u32>(entries.size()));
    for (const OutputEntry& entry : entries) {
        out.writeU32(entry.nameIndex);
        out.writeU8(static_cast<u8>(entry.kind));
        out.writeU8(entry.list ? 1 : 0);
        out.writeU16(entry.owner);
    }
}

void writePartsSection(Writer& out, std::span<const PartEntry> entries) {
    out.writeU32(static_cast<u32>(entries.size()));
    for (const PartEntry& entry : entries) {
        out.writeU32(entry.nameIndex);
        out.writeU32(entry.firstFunction);
        out.writeU32(entry.functionCount);
        out.writeU32(entry.parent);
    }
}

void writeModelSection(Writer& out, const f32 size[3], f32 unitsPerMeter) {
    for (int i = 0; i < 3; ++i) out.writeF32(size[i]);
    out.writeF32(unitsPerMeter);
    for (int i = 0; i < 4; ++i) out.writeU32(0); // flags and reserved
}

void writeSkinsSection(Writer& out, std::span<const SkinEntry> entries) {
    out.writeU32(static_cast<u32>(entries.size()));
    // Identical programs are stored once.
    struct Placed {
        std::span<const u8> bytes;
        u32 offset = 0;
    };
    std::vector<Placed> placed;
    std::vector<u32> offsets(entries.size());
    u32 next = 4u + static_cast<u32>(entries.size()) * 12u;
    for (std::size_t i = 0; i < entries.size(); ++i) {
        const auto bytes = entries[i].bytes;
        const auto found = std::find_if(placed.begin(), placed.end(), [&](const Placed& other) {
            return other.bytes.size() == bytes.size() && std::equal(bytes.begin(), bytes.end(), other.bytes.begin());
        });
        if (found != placed.end()) {
            offsets[i] = found->offset;
            continue;
        }
        offsets[i] = next;
        placed.push_back({bytes, next});
        next += static_cast<u32>((bytes.size() + 3u) & ~std::size_t{3});
    }
    for (std::size_t i = 0; i < entries.size(); ++i) {
        out.writeU32(entries[i].nameIndex);
        out.writeU32(offsets[i]);
        out.writeU32(static_cast<u32>(entries[i].bytes.size()));
    }
    for (const Placed& program : placed) {
        out.writeBytes(program.bytes);
        for (std::size_t pad = program.bytes.size(); pad % 4 != 0; ++pad) out.writeU8(0);
    }
}

} // namespace iprop::model
