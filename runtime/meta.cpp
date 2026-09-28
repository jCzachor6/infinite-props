#include "iprop/Meta.hpp"

#include "iprop/Binary.hpp"

#include <set>

namespace iprop {

const InputDecl* AssetMeta::findInput(std::string_view name, const ConstPool& pool) const {
    for (const auto& input : inputs) {
        if (pool.strings[input.nameIndex] == name) {
            return &input;
        }
    }
    return nullptr;
}

const OutputDecl* AssetMeta::findOutput(std::string_view name, const ConstPool& pool) const {
    for (const auto& output : outputs) {
        if (pool.strings[output.nameIndex] == name) {
            return &output;
        }
    }
    return nullptr;
}

AssetMeta parseMeta(std::span<const u8> data, const Section& section, const ConstPool& pool) {
    if (section.id != SectionId::Meta) {
        throw FormatError("not a meta section", section.offset);
    }
    Reader reader = sectionReader(data, section);
    AssetMeta meta;

    meta.sizeX = reader.readU32();
    meta.sizeY = reader.readU32();
    if (meta.sizeX == 0 || meta.sizeY == 0) {
        throw FormatError("canvas size must be positive", reader.position() - 8);
    }

    meta.origin = {reader.readF32(), reader.readF32()};

    meta.variants = reader.readU32();
    meta.archetypes = reader.readU32();
    if (meta.variants == 0 || meta.archetypes == 0) {
        throw FormatError("variants and archetypes must be at least 1", reader.position() - 8);
    }

    meta.fuel = reader.readU64();
    if (meta.fuel == 0) {
        throw FormatError("fuel budget must be at least 1", reader.position() - 8);
    }

    const u16 flags = reader.readU16();
    if ((flags & ~u16(1u)) != 0) {
        throw FormatError("unknown meta flag bits", reader.position() - 2);
    }
    meta.hasEmission = (flags & 1u) != 0;
    for (int i = 0; i < 6; ++i) { // reserved, MUST be 0 (spec §4)
        if (reader.readU8() != 0) {
            throw FormatError("reserved meta bytes must be zero", reader.position() - 1);
        }
    }
    if (meta.hasEmission) {
        meta.emission.color = reader.readColor();
        meta.emission.offset = {reader.readF32(), reader.readF32()};
        meta.emission.radius = reader.readF32();
    }

    const u16 inputCount = reader.readU16();
    meta.inputs.reserve(inputCount);
    std::set<std::string> inputNames;
    for (u16 i = 0; i < inputCount; ++i) {
        const std::size_t entryStart = reader.position();
        const u8 rawType = reader.readU8();
        if (rawType > 2) {
            throw FormatError("unknown input type", reader.position() - 1);
        }
        InputDecl decl;
        decl.type = static_cast<InputType>(rawType);
        decl.nameIndex = reader.readU32();
        if (decl.nameIndex >= pool.strings.size()) {
            throw FormatError("input name outside const string pool", reader.position() - 4);
        }
        switch (decl.type) {
        case InputType::Color: decl.colorDefault = reader.readColor(); break;
        case InputType::F32: decl.f32Default = reader.readF32(); break;
        case InputType::U32: decl.u32Default = reader.readU32(); break;
        }
        if (!inputNames.insert(pool.strings[decl.nameIndex]).second) {
            throw FormatError("duplicate input name", entryStart);
        }
        meta.inputs.push_back(decl);
    }

    const u16 outputCount = reader.readU16();
    meta.outputs.reserve(outputCount);
    std::set<std::string> outputNames;
    for (u16 i = 0; i < outputCount; ++i) {
        const std::size_t entryStart = reader.position();
        const u8 rawKind = reader.readU8();
        if (rawKind > 2) {
            throw FormatError("unknown output kind", reader.position() - 1);
        }
        const u8 listFlag = reader.readU8();
        if (listFlag > 1) {
            throw FormatError("invalid list flag", reader.position() - 1);
        }
        for (int k = 0; k < 2; ++k) { // reserved, MUST be 0 (spec §4.2)
            if (reader.readU8() != 0) {
                throw FormatError("reserved output bytes must be zero", reader.position() - 1);
            }
        }
        const u32 nameIndex = reader.readU32();
        if (nameIndex >= pool.strings.size()) {
            throw FormatError("output name outside const string pool", reader.position() - 4);
        }
        if (!outputNames.insert(pool.strings[nameIndex]).second) {
            throw FormatError("duplicate output name", entryStart);
        }
        meta.outputs.push_back({static_cast<OutputKind>(rawKind), listFlag == 1, nameIndex});
    }

    if (!reader.empty()) {
        throw FormatError("trailing bytes in meta section", reader.position());
    }

    return meta;
}

} // namespace iprop
