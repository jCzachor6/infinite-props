#include "iprop/Debug.hpp"

#include "iprop/Binary.hpp"
#include "iprop/Compression.hpp"

namespace iprop {

u32 debugLineAt(std::span<const u8> data, const Section& section,
                u32 functionIndex, u32 pc, u32 functionCount, u32 instructionCount) noexcept {
    if (section.id != SectionId::Debug || section.offset > data.size() ||
        section.size > data.size() - section.offset || functionIndex >= functionCount ||
        pc >= instructionCount) return 0;
    const auto bytes = data.subspan(section.offset, section.size);
    std::size_t at = 0;
    const auto read = [&bytes, &at](u32& value) {
        if (at > bytes.size() || bytes.size() - at < 4) return false;
        value = static_cast<u32>(bytes[at]) | (static_cast<u32>(bytes[at + 1]) << 8) |
                (static_cast<u32>(bytes[at + 2]) << 16) | (static_cast<u32>(bytes[at + 3]) << 24);
        at += 4;
        return true;
    };
    const auto skip = [&bytes, &at](u32 words) {
        if (at > bytes.size() || words > (bytes.size() - at) / 4) return false;
        at += static_cast<std::size_t>(words) * 4;
        return true;
    };
    u32 count = 0;
    if (!read(count) || count != functionCount || count > kMaxFunctions) return 0;
    u32 line = 0;
    for (u32 function = 0; function < count; ++function) {
        u32 locals = 0, instructions = 0;
        if (!read(locals) || locals > kMaxLocals || !skip(locals) || !read(instructions) ||
            instructions > kMaxSectionSize / 4) return 0;
        if (function == functionIndex) {
            if (instructions != instructionCount) return 0;
            const auto begin = at;
            if (!skip(instructions)) return 0;
            const auto end = at;
            at = begin + static_cast<std::size_t>(pc) * 4;
            if (!read(line)) return 0;
            at = end;
        } else {
            if (!skip(instructions)) return 0;
        }
    }
    return at == bytes.size() ? line : 0;
}

DebugInfo parseDebug(std::span<const u8> data, const Section& section) {
    if (section.id != SectionId::Debug) {
        throw FormatError("not a debug section", section.offset, ErrorReason::InvalidDebug);
    }
    Reader reader = sectionReader(data, section);
    DebugInfo info;

    const u32 functionCount = reader.readU32();
    if (functionCount > kMaxFunctions) {
        throw FormatError("debug function count out of range", reader.position() - 4, ErrorReason::InvalidDebug);
    }
    info.functions.reserve(functionCount);
    for (u32 f = 0; f < functionCount; ++f) {
        DebugFunction function;
        const u32 localCount = reader.readU32();
        if (localCount > kMaxLocals) {
            throw FormatError("debug local count out of range", reader.position() - 4, ErrorReason::InvalidDebug);
        }
        function.localNames.reserve(localCount);
        for (u32 i = 0; i < localCount; ++i) {
            function.localNames.push_back(reader.readU32());
        }
        const u32 instructionCount = reader.readU32();
        if (instructionCount > kMaxSectionSize / 4) {
            throw FormatError("debug instruction count out of range",
                              reader.position() - 4, ErrorReason::InvalidDebug);
        }
        function.lines.reserve(instructionCount);
        for (u32 i = 0; i < instructionCount; ++i) {
            function.lines.push_back(reader.readU32());
        }
        info.functions.push_back(std::move(function));
    }

    if (!reader.empty()) {
        throw FormatError("trailing bytes in debug section", reader.position(), ErrorReason::InvalidDebug);
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
