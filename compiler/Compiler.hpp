#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "iprop/Types.hpp"
#include "ips/Ast.hpp"

namespace iprop {

// Compiles a semantically-checked .ips program — the entry asset plus every
// part instance, keyed by alias path ("hilt", "hilt.grip") — into .iprop
// bytes (spec §2–§6). Optimisation is enabled by default; false retains the
// reference lowering. Diagnostics in a part name its file from partSources
// (by alias path), else its import path resolved lexically. The output passes
// the runtime validator before being returned, so a valid return value is
// guaranteed executable.
//
// Throws ips::SourceError with a located message for problems the semantic
// checker does not cover: non-constant origin expressions, more than 256 part
// instances, and a `draw` that returns a value. A program compiled without
// checkAsset also gets located errors for unstorable slots and for layer/clip
// blocks nested past the runtime scope stack.
std::vector<u8> compileProgram(
    const ips::Program& entry,
    const std::map<std::string, const ips::Program*>& parts,
    bool optimize = true, const std::map<std::string, std::string>& partSources = {});

} // namespace iprop
