#pragma once

#include "iprop/Binary.hpp"
#include "iprop/Constants.hpp"
#include "iprop/Format.hpp"
#include "iprop/Types.hpp"

#include <cstddef>
#include <span>
#include <vector>

namespace iprop {

// A bounded Reader over a section payload; errors report absolute file
// offsets (shared by every section parser).
inline Reader sectionReader(std::span<const u8> data, const Section& section) {
    return Reader(data, section.offset, static_cast<std::size_t>(section.offset) + section.size);
}

// Parsed `const` section (spec §5). Indices are 0-based per pool; the i32
// pool also serves as the u32 constant pool.
struct ConstPool {
    std::vector<std::string> strings;
    std::vector<i32> ints;
    std::vector<f32> floats;
    std::vector<Color> colors;
};

// Parses the `const` section (spec §5). Throws located FormatError on any
// violation (spec §14: string lengths within section, UTF-8 well-formedness,
// pool size limits, no trailing bytes).
ConstPool parseConsts(std::span<const u8> data, const Section& section);

} // namespace iprop
