#pragma once

// S-expression rendering of the AST: a debugging aid and a stable,
// human-readable form for golden tests. The output is deterministic.

#include <string>

#include "ips/Ast.hpp"

namespace iprop::ips {

std::string printProgram(const Program& program);

} // namespace iprop::ips
