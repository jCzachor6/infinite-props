#pragma once

// The `debug` section (spec §7): informative, implementation-specific.
// Contents MUST NOT affect execution; stripping the section MUST produce a
// byte-identical file modulo the header flag and the section table.
//
// This implementation stores, per function (in table order): the source
// names of the locals (string-pool indices, 0xFFFFFFFF = unnamed) and the
// `.ips` source line of every instruction (0 = none).

#include <cstdint>
#include <span>
#include <vector>

#include "iprop/Constants.hpp"
#include "iprop/Format.hpp"
#include "iprop/Types.hpp"

namespace iprop {

struct DebugFunction {
    std::vector<u32> localNames; // string-pool indices; 0xFFFFFFFF = unnamed
    std::vector<u32> lines;      // one .ips source line per instruction
};

struct DebugInfo {
    std::vector<DebugFunction> functions; // one per code-section function
};

// Parses a debug section. Throws located FormatError on malformed payloads
// (counts within section bounds, no trailing bytes). The consumer cross-
// checks the per-function instruction counts against the code section.
DebugInfo parseDebug(std::span<const u8> data, const Section& section);

// Removes the debug section from a file: clears the header flag, drops the
// section table entry and shifts the following sections. The remaining
// bytes are untouched, so execution is identical. Compressed envelopes are
// expanded first and may be recompressed after stripping. Returns the input
// unchanged when there is no debug section. Throws FormatError for files
// that do not parse.
std::vector<u8> stripDebug(std::span<const u8> data);

} // namespace iprop
