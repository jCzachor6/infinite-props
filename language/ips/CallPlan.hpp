#pragma once

#include "ips/BuiltinArguments.hpp"

#include <map>
#include <unordered_map>
#include <variant>

namespace iprop::ips {
struct PartInput;

// A declaration reference defers uniform default validation to the declaration
// pass. Constructing an imported surface must never fold its defaults.
using ArgumentDefault = std::variant<std::monostate, i32, const InputDecl*>;
struct CallSignature {
    std::vector<ArgumentParameter> parameters;
    std::vector<ArgumentDefault> defaults;
    // Slot indices in evaluation order, including compile-time-only slots.
    // Polygon coordinates precede color; part uniforms precede seed/v/a/at.
    std::vector<std::size_t> evaluationOrder;
};
struct BoundCallPlan {
    CallSignature signature;
    std::vector<const Arg*> arguments; // signature slots, null for defaults
};
using CallPlans = std::unordered_map<const Call*, BoundCallPlan>;

CallSignature builtinCallSignature(const BuiltinSpec& spec, std::size_t arity,
    const SourceLocation& at, std::string_view function);
CallSignature helperCallSignature(std::span<const std::string> names);
CallSignature rampCallSignature();
CallSignature partCallSignature(std::span<const PartInput> inputs);
BoundCallPlan bindCall(const Call& call, CallSignature signature, std::string_view function);

// The source-only formatter has local declarations but may not have imports.
// Unknown surfaces retain written order rather than guessing uniform names.
std::optional<BoundCallPlan> sourceCallPlan(const Program& program, const Call& call,
    const std::map<std::string, const Program*>& parts = {});
struct PrintedArgument {
    const Arg* argument;
    std::optional<std::string> name;
};
std::vector<PrintedArgument> printedCallArguments(const Call& call,
    const BoundCallPlan* plan);
} // namespace iprop::ips
