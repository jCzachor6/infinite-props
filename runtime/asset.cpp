#include "iprop/Asset.hpp"

#include "iprop/Binary.hpp"

#include <set>

namespace iprop {
namespace {

// RFC 3629 well-formedness: no overlong encodings, no surrogates, no values
// above U+10FFFF, correct continuation bytes.
bool validUtf8(std::span<const u8> bytes) {
    std::size_t i = 0;
    while (i < bytes.size()) {
        const u8 lead = bytes[i];
        if (lead < 0x80) {
            ++i;
            continue;
        }
        std::size_t len = 0;
        u32 cp = 0;
        if ((lead & 0xE0) == 0xC0) {
            len = 1;
            cp = lead & 0x1Fu;
        } else if ((lead & 0xF0) == 0xE0) {
            len = 2;
            cp = lead & 0x0Fu;
        } else if ((lead & 0xF8) == 0xF0) {
            len = 3;
            cp = lead & 0x07u;
        } else {
            return false;
        }
        if (i + len >= bytes.size()) {
            return false;
        }
        for (std::size_t k = 1; k <= len; ++k) {
            const u8 cont = bytes[i + k];
            if ((cont & 0xC0) != 0x80) {
                return false;
            }
            cp = (cp << 6) | (cont & 0x3Fu);
        }
        if (cp > 0x10FFFFu || (cp >= 0xD800u && cp <= 0xDFFFu)) {
            return false;
        }
        const bool overlong = (len == 1 && cp < 0x80u) || (len == 2 && cp < 0x800u) ||
                              (len == 3 && cp < 0x10000u);
        if (overlong) {
            return false;
        }
        i += len + 1;
    }
    return true;
}

// Bounded reader over a section payload with absolute (file-level) offsets.
Reader sectionReader(std::span<const u8> data, const Section& section) {
    return Reader(data, section.offset, static_cast<std::size_t>(section.offset) + section.size);
}

} // namespace

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

ConstPool parseConsts(std::span<const u8> data, const Section& section) {
    if (section.id != SectionId::Const) {
        throw FormatError("not a const section", section.offset);
    }
    Reader reader = sectionReader(data, section);
    ConstPool pool;

    const u32 stringCount = reader.readU32();
    if (stringCount > kMaxConstPoolEntries) {
        throw FormatError("string pool too large", reader.position() - 4);
    }
    pool.strings.reserve(stringCount);
    for (u32 i = 0; i < stringCount; ++i) {
        const u32 len = reader.readU32();
        if (len > reader.remaining()) {
            throw FormatError("string length exceeds section", reader.position() - 4);
        }
        const auto bytes = reader.readBytes(len);
        if (!validUtf8(bytes)) {
            throw FormatError("string is not valid UTF-8", reader.position() - len);
        }
        pool.strings.emplace_back(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    }

    const u32 intCount = reader.readU32();
    if (intCount > kMaxConstPoolEntries) {
        throw FormatError("int pool too large", reader.position() - 4);
    }
    pool.ints.reserve(intCount);
    for (u32 i = 0; i < intCount; ++i) {
        pool.ints.push_back(reader.readI32());
    }

    const u32 floatCount = reader.readU32();
    if (floatCount > kMaxConstPoolEntries) {
        throw FormatError("float pool too large", reader.position() - 4);
    }
    pool.floats.reserve(floatCount);
    for (u32 i = 0; i < floatCount; ++i) {
        pool.floats.push_back(reader.readF32());
    }

    const u32 colorCount = reader.readU32();
    if (colorCount > kMaxConstPoolEntries) {
        throw FormatError("color pool too large", reader.position() - 4);
    }
    pool.colors.reserve(colorCount);
    for (u32 i = 0; i < colorCount; ++i) {
        pool.colors.push_back(reader.readColor());
    }

    return pool;
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
    reader.skip(6); // reserved, must be zero — pinned by the format
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
        reader.skip(2); // reserved
        const u32 nameIndex = reader.readU32();
        if (nameIndex >= pool.strings.size()) {
            throw FormatError("output name outside const string pool", reader.position() - 4);
        }
        if (!outputNames.insert(pool.strings[nameIndex]).second) {
            throw FormatError("duplicate output name", entryStart);
        }
        meta.outputs.push_back({static_cast<OutputKind>(rawKind), listFlag == 1, nameIndex});
    }

    return meta;
}

} // namespace iprop
