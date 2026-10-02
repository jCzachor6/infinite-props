#pragma once

// Constant folding over checked .ips expressions: evaluates literal
// arithmetic, comparisons, bitwise and logical operators at compile time
// (short-circuit semantics preserved). Integer division and remainder are NOT
// folded so the runtime's divide-by-zero trap stays observable.

#include "iprop/Value.hpp"
#include "ips/Ast.hpp"
#include <optional>
#include <unordered_map>

namespace iprop::ips {

// One cache per immutable expression graph. Failed folds are cached too,
// so a dynamic leaf in a long arithmetic chain never causes quadratic work.
class ConstantFolder {
public:
    bool fold(const Expr& expr, Value& out);
private:
    std::unordered_map<const Expr*, std::optional<Value>> values_;
    bool evaluate(const Expr& expr, Value& out);
};

// If `expr` is a constant (literal or folded operator tree), evaluates it
// and returns true with the value filled in. Otherwise returns false and
// leaves `out` untouched.
bool foldExpression(const ips::Expr& expr, Value& out);

} // namespace iprop::ips
