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
    u32 flags = 0;
    u32 sectionCount = 0;

    bool hasDebug() const { return (flags & kFlagDebug) != 0; }
    bool hasCompression() const { return (flags & kFlagCompressed) != 0; }
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

// The file family a reader admits. Every function defaults to the 2D format;
// the model runtime, a separate library, passes its own dialect (spec models
// v1.2). A dialect changes what is accepted, never how an accepted byte is read.
struct Dialect {
    u8 magic[4];
    u32 lastSection; // the highest admitted section id
    bool vec3;       // admits the vec3 type code in function signatures
};
inline constexpr Dialect kPropDialect{{kMagic[0], kMagic[1], kMagic[2], kMagic[3]},
                                      static_cast<u32>(SectionId::Steppers), false};

// Parses the fixed header (including version/flags/count/reserved checks).
Header parseHeader(std::span<const u8> data, OffsetSpace space = OffsetSpace::Container,
                   const Dialect& dialect = kPropDialect);

// Parses and validates an expanded file header and section table.
// Compressed envelopes must first pass through decompressFile().
// Throws FormatError with a file offset on every violation of spec §2/§14.
// Sections above SectionId::Steppers that the dialect admits are listed in
// the layout and left to the dialect's own reader.
FileLayout parseLayout(std::span<const u8> data, const Dialect& dialect = kPropDialect);

} // namespace iprop
