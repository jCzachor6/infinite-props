#pragma once

#include "iprop/D4Transform.hpp"
#include "ips/BuiltinSpec.hpp"
#include "ips/Palette.hpp"

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
    // In evaluation/VM push order: a polygon's vertices, then its color.
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
    std::optional<PlannedArgument> seed = std::nullopt;
};
struct PartCall {
    u32 part;
    D4Transform transform;
    struct Placement { const ips::Expr* expression; };
    // Lowered from the frontend's explicit evaluation order. Placement is
    // pushed as two coordinates; defaults are already resolved values.
    std::vector<std::variant<PlannedArgument, Value, Placement>> arguments;
};
// `alias.draw(...)` of a model (models v1.2): a texture of the imported asset.
struct SkinCall {
    u32 program;
    // The inputs in declaration order, supplied or at their default.
    std::vector<std::variant<PlannedArgument, Value>> inputs;
    PlannedArgument seed;
    const ips::Expr* size = nullptr; // a vec2 of pixels; null: the declared size
};
struct RampCall {
    const ips::ResolvedPalette* palette;
    PlannedArgument position;
    std::optional<Value> constant;
};
struct ResolvedCall {
    ips::SourceLocation location;
    std::variant<BuiltinCall, UserCall, PartCall, RampCall, SelectionCall, SkinCall> target;
};

// Lowers frontend-bound calls to coercions, defaults and table indices.
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
    PartCall part(const ips::Call& call, const std::string& path) const;
    SkinCall skin(const ips::Call& call, const std::string& alias) const;
};
} // namespace iprop::compiler
