#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "iprop/Types.hpp"
#include "ips/Ast.hpp"

namespace iprop {

// Compiles a semantically-checked .ips program — the entry asset plus its
// imported parts, keyed by import alias — into .iprop bytes (spec §2–§6).
// The output passes the runtime validator before being returned, so a valid
// return value is guaranteed executable.
//
// Throws ips::SourceError with a located message for problems the semantic
// checker does not cover: non-constant origin expressions, `input i32`
// (the v0.1 format has no i32 uniform type), parts that import parts, and
// a `draw` that returns a value.
std::vector<u8> compileProgram(
    const ips::Program& entry,
    const std::map<std::string, const ips::Program*>& parts);

} // namespace iprop
