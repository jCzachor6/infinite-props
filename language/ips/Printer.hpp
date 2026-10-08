#pragma once

// Canonical .ips source printing, the formatter behind `ipc --format`.

#include <string>
#include <map>

#include "ips/Ast.hpp"

namespace iprop::ips {

// Canonical, parseable .ips source with owning token/range trivia.
// Calls containing comments retain written argument order. Other known calls
// use their shared evaluation plan; unknown imports keep written order.
// Imported declarations are optional; reformatting is idempotent.
std::string formatProgram(const Program& program,
    const std::map<std::string, const Program*>& parts = {});

} // namespace iprop::ips
