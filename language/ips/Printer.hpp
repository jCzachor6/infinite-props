#pragma once

// S-expression rendering of the AST: a debugging aid and a stable,
// human-readable form for golden tests. The output is deterministic.

#include <string>
#include <map>

#include "ips/Ast.hpp"

namespace iprop::ips {

std::string printProgram(const Program& program);

// Canonical, parseable .ips source with owning token/range trivia.
// Calls containing comments retain written argument order. Other known calls
// use their shared evaluation plan; unknown imports keep written order.
// Imported declarations are optional; reformatting is idempotent.
std::string formatProgram(const Program& program,
    const std::map<std::string, const Program*>& parts = {});

} // namespace iprop::ips
