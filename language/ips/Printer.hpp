#pragma once

// S-expression rendering of the AST: a debugging aid and a stable,
// human-readable form for golden tests. The output is deterministic.

#include <string>

#include "ips/Ast.hpp"

namespace iprop::ips {

std::string printProgram(const Program& program);

// Canonical, parseable .ips source. Comments are absent from the AST.
// Named builtins use signature order; reformatting is idempotent.
std::string formatProgram(const Program& program);

} // namespace iprop::ips
