#pragma once

#include "iprop/Binary.hpp"
#include "iprop/Constants.hpp"
#include "iprop/Types.hpp"

#include <cstddef>
#include <span>
#include <vector>

namespace iprop {

// Parsed file header (spec §2.1).
struct Header {
    u16 versionMajor = 0;
    u16 versionMinor = 0;
    u32 flags = 0;

    bool hasDebug() const { return (flags & kFlagDebug) != 0; }
};

// One section table entry (spec §2.2).
struct Section {
    SectionId id = SectionId::Meta;
    u32 offset = 0; // from the start of the file
    u32 size = 0;   // bytes
};

// A bounded Reader over a section payload; errors report absolute file
// offsets (shared by every section parser).
inline Reader sectionReader(std::span<const u8> data, const Section& section) {
    return Reader(data, section.offset, static_cast<std::size_t>(section.offset) + section.size);
}

// Header + section table of a parsed .iprop file. Section *contents* are
// parsed in later phases; this phase pins the layout contract of spec §2.
struct FileLayout {
    Header header;
    std::vector<Section> sections;
    std::size_t totalSize = 0;

    // The first section with the given id, or nullptr when absent.
    const Section* find(SectionId id) const;
};

// Parses and validates the header and section table of a complete file.
// Throws FormatError with a file offset on every violation of spec §2/§14.
FileLayout parseLayout(std::span<const u8> data);

} // namespace iprop
