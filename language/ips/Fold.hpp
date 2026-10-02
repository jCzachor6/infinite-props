#pragma once

// Constant folding over checked .ips expressions: evaluates literal
// arithmetic, comparisons, bitwise and logical operators at compile time
// (short-circuit semantics preserved). Integer division and remainder are NOT
// folded so the runtime's divide-by-zero trap stays observable.

#include "iprop/Value.hpp"
#include "ips/Ast.hpp"

namespace iprop::ips {

// If `expr` is a constant (literal or folded operator tree), evaluates it
// and returns true with the value filled in. Otherwise returns false and
// leaves `out` untouched.
bool foldExpression(const ips::Expr& expr, Value& out);

} // namespace iprop::ips
