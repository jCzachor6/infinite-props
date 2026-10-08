#include "ips/CallPlan.hpp"
#include "ips/Typing.hpp"

#include <numeric>

namespace iprop::ips {
namespace {
CallSignature ordered(std::vector<ArgumentParameter> parameters) {
    CallSignature result{std::move(parameters), {}, {}, {}};
    result.defaults.resize(result.parameters.size());
    result.evaluationOrder.resize(result.parameters.size());
    std::iota(result.evaluationOrder.begin(), result.evaluationOrder.end(), 0);
    return result;
}
std::string calleeName(const Expr& expr) {
    if (const auto* id = std::get_if<Identifier>(&expr)) return id->name;
    if (const auto* member = std::get_if<Member>(&expr)) {
        const auto base = calleeName(*member->object);
        if (!base.empty()) return base + "." + member->name;
    }
    return {};
}
}

CallSignature helperCallSignature(std::span<const std::string> names) {
    std::vector<ArgumentParameter> parameters;
    for (const auto& name : names) parameters.push_back({name});
    auto result = ordered(std::move(parameters));
    result.effects = findOpcode(Opcode::Call)->effects;
    return result;
}
CallSignature builtinCallSignature(const BuiltinSpec& spec, std::size_t arity,
    const SourceLocation& at, std::string_view function) {
    auto result = helperCallSignature(builtinParameterNames(spec, arity, at, function));
    result.effects = builtinEffects(spec, builtinOverload(spec, arity));
    if (spec.lowering == BuiltinLowering::Polygon) {
        result.evaluationOrder.erase(result.evaluationOrder.begin());
        result.evaluationOrder.push_back(0);
    }
    return result;
}
CallSignature rampCallSignature() {
    auto result = helperCallSignature(rampParameterNames());
    result.effects = {};
    return result;
}
CallSignature partCallSignature(std::span<const PartInput> inputs) {
    std::vector<std::string> names;
    for (const auto& input : inputs) names.push_back(input.name);
    auto result = ordered(partParameters(names));
    result.effects = findOpcode(Opcode::PartDraw)->effects;
    result.evaluationOrder.clear();
    for (std::size_t i = 0; i < inputs.size(); ++i) {
        result.evaluationOrder.push_back(7 + i);
        result.defaults[7 + i] = inputs[i].declaration;
    }
    for (std::size_t i = 0; i < 7; ++i) result.evaluationOrder.push_back(i);
    for (std::size_t i = 4; i < 7; ++i) result.defaults[i] = i32{0};
    return result;
}
BoundCallPlan bindCall(const Call& call, CallSignature signature, std::string_view function) {
    auto arguments = bindArguments(call, signature.parameters, function);
    return {std::move(signature), std::move(arguments)};
}

std::optional<BoundCallPlan> sourceCallPlan(const Program& program, const Call& call,
    const std::map<std::string, const Program*>& parts) {
    const auto name = calleeName(*call.callee);
    const auto declared = [&] {
        for (const auto& function : program.asset.functions)
            if (function.name == name) return true;
        return false;
    };
    if (const auto found = builtinSpecs().find(name);
        found != builtinSpecs().end() && !(builtinYieldsToHelper(name) && declared()))
        return bindCall(call, builtinCallSignature(found->second, call.args.size(), call.location, name), name);
    if (name == "ramp") return bindCall(call, rampCallSignature(), name);
    for (const auto& function : program.asset.functions) {
        if (name == function.name && name != "draw")
            return bindCall(call, helperCallSignature(declaredParamNames(function)), name);
    }
    if (name.ends_with(".draw")) {
        const auto found = parts.find(name.substr(0, name.size() - 5));
        if (found != parts.end()) {
            std::vector<PartInput> inputs;
            for (const auto& input : found->second->asset.inputs)
                inputs.push_back({input.type, input.name, &input});
            return bindCall(call, partCallSignature(inputs), name);
        }
    }
    return std::nullopt;
}

std::vector<PrintedArgument> printedCallArguments(const Call& call, const BoundCallPlan* plan) {
    std::vector<PrintedArgument> result;
    if (!plan) {
        for (const auto& arg : call.args) result.push_back({&arg, arg.name});
        return result;
    }
    std::size_t positional = 0;
    std::vector<std::size_t> positionalSlots;
    for (std::size_t i = 0; i < plan->signature.parameters.size(); ++i)
        if (plan->signature.parameters[i].positional) positionalSlots.push_back(i);
    // Preserve legacy positional spelling whenever its ordinal still maps to
    // the same slot. Moved polygon slots become named to retain their meaning.
    for (const auto slot : plan->signature.evaluationOrder) {
        if (const auto* arg = plan->arguments[slot]) {
            auto name = arg->name;
            if (!name) {
                if (positional < positionalSlots.size() && positionalSlots[positional] == slot) ++positional;
                else name = plan->signature.parameters[slot].name;
            }
            result.push_back({arg, std::move(name)});
        }
    }
    return result;
}
} // namespace iprop::ips
