#pragma once

// Small declaration-only constant grammar. This is deliberately separate
// from general constant folding: arithmetic, calls and RNG are not defaults
// or palette elements even when an expression could be folded.

#include <optional>

#include "iprop/Value.hpp"
#include "ips/Ast.hpp"

namespace iprop::ips {

struct ResolvedConstant {
    Value value;
    Literal::Kind kind = Literal::Kind::Int;
    // Mathematical integer before 32-bit wrapping, for future signed-input
    // range checks. Ordinary expression/palette integer semantics stay i32.
    std::optional<i64> mathematicalInteger;
};

ResolvedConstant resolveDeclarationConstant(const Expr& expression);
ResolvedConstant resolveInputDefault(const InputDecl& input);

} // namespace iprop::ips
