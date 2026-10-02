#pragma once

#include "iprop/D4Transform.hpp"
#include "ips/BuiltinSpec.hpp"
#include "ips/Palette.hpp"

#include <array>
#include <variant>

namespace iprop::compiler {
struct Plan;
struct PlannedFunction;

struct PlannedArgument {
    const ips::Expr* expression;
    ips::ValueType expected;
    bool widen;
};

struct BuiltinCall {
    const ips::BuiltinSpec* spec;
    const ips::BuiltinOverload* overload; // null only for variadic polygon
    // In evaluation/VM push order, including polygon's vertices before colour.
    std::vector<PlannedArgument> arguments;
};
struct UserCall {
    u32 function;
    ips::ValueType result;
    std::vector<PlannedArgument> arguments;
};
struct SelectionCall {
    ips::ValueType result;
    std::vector<PlannedArgument> choices;
};
struct PartCall {
    u32 part;
    D4Transform transform;
    // Uniforms in declaration order, followed by seed/variant/archetype and at.
    std::vector<std::variant<PlannedArgument, Value>> uniforms;
    std::array<PlannedArgument, 3> positional;
    const ips::Expr* at;
};
struct RampCall {
    const ips::ResolvedPalette* palette;
    PlannedArgument position;
    std::optional<Value> constant;
};
struct ResolvedCall {
    ips::SourceLocation location;
    std::variant<BuiltinCall, UserCall, PartCall, RampCall, SelectionCall> target;
};

// Binds checked calls to signatures, coercions, defaults and table indices.
// It has no code-emission state; the emitter consumes only the returned plan.
class CallResolver {
public:
    CallResolver(const Plan& plan, const PlannedFunction& function)
        : plan_(plan), function_(function) {}
    ResolvedCall resolve(const ips::Call& call) const;
private:
    const Plan& plan_;
    const PlannedFunction& function_;
    PlannedArgument argument(const ips::Expr& expression, ips::ValueType expected) const;
    PartCall part(const ips::Call& call, const std::string& alias) const;
};
} // namespace iprop::compiler
