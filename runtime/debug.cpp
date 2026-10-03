#include "iprop/Debug.hpp"

#include "iprop/Binary.hpp"
#include "iprop/Compression.hpp"

namespace iprop {

DebugInfo parseDebug(std::span<const u8> data, const Section& section) {
    if (section.id != SectionId::Debug) {
        throw FormatError("not a debug section", section.offset);
    }
    Reader reader = sectionReader(data, section);
    DebugInfo info;

    const u32 functionCount = reader.readU32();
    if (functionCount > kMaxFunctions) {
        throw FormatError("debug function count out of range", reader.position() - 4);
    }
    info.functions.reserve(functionCount);
    for (u32 f = 0; f < functionCount; ++f) {
        DebugFunction function;
        const u32 localCount = reader.readU32();
        if (localCount > kMaxLocals) {
            throw FormatError("debug local count out of range", reader.position() - 4);
        }
        function.localNames.reserve(localCount);
        for (u32 i = 0; i < localCount; ++i) {
            function.localNames.push_back(reader.readU32());
        }
        const u32 instructionCount = reader.readU32();
        if (instructionCount > kMaxSectionSize / 4) {
            throw FormatError("debug instruction count out of range",
                              reader.position() - 4);
        }
        function.lines.reserve(instructionCount);
        for (u32 i = 0; i < instructionCount; ++i) {
            function.lines.push_back(reader.readU32());
        }
        info.functions.push_back(std::move(function));
    }

    if (!reader.empty()) {
        throw FormatError("trailing bytes in debug section", reader.position());
    }
    return info;
}

std::vector<u8> stripDebug(std::span<const u8> data) {
    if (parseHeader(data).hasCompression()) {
        const auto expanded = decompressFile(data);
        if (!parseLayout(expanded).header.hasDebug()) return {data.begin(), data.end()};
        return compressFile(stripDebug(expanded));
    }
    const FileLayout layout = parseLayout(data);
    if (!layout.header.hasDebug()) {
        return std::vector<u8>(data.begin(), data.end());
    }
    const Section* debug = layout.find(SectionId::Debug);
    if (debug == nullptr) {
        // The flag said otherwise; parseLayout already rejected that.
        return std::vector<u8>(data.begin(), data.end());
    }

    Writer file;
    for (const u8 magic : kMagic) {
        file.writeU8(magic);
    }
    file.writeU16(layout.header.versionMajor);
    file.writeU16(layout.header.versionMinor);
    file.writeU32(0); // flags: debug cleared
    file.writeU32(static_cast<u32>(layout.sections.size() - 1));
    file.writeU32(0); // reserved

    for (const Section& section : layout.sections) {
        if (section.id == SectionId::Debug) {
            continue;
        }
        // The section table shrinks by one entry (12 bytes), so every
        // section shifts left by 12; sections after the debug payload also
        // shift by its size.
        u32 offset = section.offset - 12;
        if (section.offset > debug->offset) {
            offset -= debug->size;
        }
        file.writeU32(static_cast<u32>(section.id));
        file.writeU32(offset);
        file.writeU32(section.size);
    }

    // The section payloads, in table order (the canonical layout).
    for (const Section& section : layout.sections) {
        if (section.id == SectionId::Debug) {
            continue;
        }
        file.writeBytes(data.subspan(section.offset, section.size));
    }
    return file.data();
}

} // namespace iprop
