#include "iprop/Format.hpp"

#include "iprop/Binary.hpp"

#include <algorithm>
#include <numeric>
#include <string>

namespace iprop {

const Section* FileLayout::find(SectionId id) const {
    for (const auto& section : sections) {
        if (section.id == id) {
            return &section;
        }
    }
    return nullptr;
}

FileLayout parseLayout(std::span<const u8> data) {
    if (data.size() < kHeaderSize) {
        throw FormatError("file too small: header truncated", data.size());
    }

    Reader reader(data);

    // Magic (spec §2.1).
    for (std::size_t i = 0; i < 4; ++i) {
        if (reader.readU8() != kMagic[i]) {
            throw FormatError("bad magic: not an .iprop file", i);
        }
    }

    FileLayout layout;
    layout.totalSize = data.size();

    // Version: interpreters reject files they cannot fully honour. This
    // implementation knows 0.x with x <= 1.
    layout.header.versionMajor = reader.readU16();
    if (layout.header.versionMajor > kVersionMajor) {
        throw FormatError("unsupported format major version", 4);
    }
    layout.header.versionMinor = reader.readU16();
    if (layout.header.versionMinor > kVersionMinor) {
        throw FormatError("unsupported format minor version", 6);
    }

    layout.header.flags = reader.readU32();
    if ((layout.header.flags & ~kFlagDebug) != 0) {
        throw FormatError("unknown header flag bits", 8);
    }

    const u32 count = reader.readU32();
    if (count < 1 || count > 255) {
        throw FormatError("section count out of range (1-255)", 12);
    }

    if (reader.readU32() != 0) {
        throw FormatError("reserved header field must be zero", 16);
    }

    // Section table (spec §2.2).
    const std::size_t tableBytes = static_cast<std::size_t>(count) * 12;
    if (kHeaderSize + tableBytes > data.size()) {
        throw FormatError("section table truncated", data.size());
    }

    layout.sections.reserve(count);
    bool seen[5] = {}; // section ids 1..4
    for (u32 i = 0; i < count; ++i) {
        const std::size_t entryAt = kHeaderSize + static_cast<std::size_t>(i) * 12;
        const u32 rawId = reader.readU32();
        const u32 offset = reader.readU32();
        const u32 size = reader.readU32();

        if (rawId < 1 || rawId > 4) {
            throw FormatError("unknown section id " + std::to_string(rawId), entryAt);
        }
        if (seen[rawId]) {
            throw FormatError("duplicate section", entryAt);
        }
        seen[rawId] = true;

        // Bounds check in u64: offset + size cannot wrap past the file.
        const u64 end = static_cast<u64>(offset) + static_cast<u64>(size);
        if (offset > data.size() || end > data.size()) {
            throw FormatError("section outside file bounds", entryAt);
        }

        layout.sections.push_back({static_cast<SectionId>(rawId), offset, size});
    }

    // Sections MUST NOT overlap (spec §2.2).
    std::vector<std::size_t> order(count);
    std::iota(order.begin(), order.end(), 0);
    std::sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
        return layout.sections[a].offset < layout.sections[b].offset;
    });
    for (std::size_t i = 1; i < order.size(); ++i) {
        const auto& prev = layout.sections[order[i - 1]];
        const auto& cur = layout.sections[order[i]];
        const u64 prevEnd = static_cast<u64>(prev.offset) + static_cast<u64>(prev.size);
        if (prevEnd > cur.offset) {
            throw FormatError("sections overlap", kHeaderSize + order[i] * 12);
        }
    }

    // meta, const and code are REQUIRED (spec §2.2).
    if (!seen[1] || !seen[2] || !seen[3]) {
        throw FormatError("missing required section (meta, const, code)", 0);
    }

    // The debug flag must agree with the presence of a debug section (§14).
    const bool debugFlag = (layout.header.flags & kFlagDebug) != 0;
    if (debugFlag != seen[4]) {
        throw FormatError("debug flag does not match debug section presence", 0);
    }

    return layout;
}

} // namespace iprop
