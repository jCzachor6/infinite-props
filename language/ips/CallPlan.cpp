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
    return result;
}
CallSignature rampCallSignature() {
    auto result = helperCallSignature(rampParameterNames());
    result.effects = {};
    return result;
}
CallSignature builtinCallSignature(const BuiltinSpec& spec, const Call& call,
    std::string_view function) {
    for (const auto& overload : spec.overloads) {
        if (overload.params.size() != call.args.size()) continue;
        const BuiltinSpec concrete{spec.lowering, {overload}};
        auto signature = builtinCallSignature(concrete, call.args.size(), call.location, function);
        try {
            (void)bindArguments(call, signature.parameters, function);
            return signature;
        } catch (const SourceError&) { /* try the other parameter-name layout */ }
    }
    return builtinCallSignature(spec, call.args.size(), call.location, function);
}
const BuiltinOverload& paintOverload(const BoundCallPlan& bound, ValueType source) {
    for (const auto& candidate : builtinSpecs().at("surface.paint").overloads) {
        if (candidate.params.front().type != source || candidate.params.size() != bound.arguments.size()) continue;
        if (std::equal(candidate.params.begin(), candidate.params.end(), bound.signature.parameters.begin(),
            [](const auto& a, const auto& b) { return a.name == b.name; })) return candidate;
    }
    throw std::logic_error("bound surface.paint has no matching overload");
}
CallSignature surfacePaintCallSignature(const Call& call, ValueType imageType) {
    auto spec = builtinSpecs().at("surface.paint");
    std::erase_if(spec.overloads, [imageType](const auto& overload) {
        return overload.params.front().type != imageType;
    });
    // A bare mode constant selects blend. A number in the same positional
    // slot selects opacity; named arguments keep their explicit meaning.
    for (const auto& overload : spec.overloads) {
        if (overload.params.size() != call.args.size()) continue;
        const BuiltinSpec concrete{spec.lowering, {overload}};
        auto signature = builtinCallSignature(concrete, call.args.size(), call.location, "surface.paint");
        try {
            const auto args = bindArguments(call, signature.parameters, "surface.paint");
            bool matches = true;
            for (std::size_t i = 0; i < args.size(); ++i) {
                if (!args[i]->name && overload.params[i].name == "blend" && !surfaceMode(*args[i]->value)) matches = false;
            }
            if (matches) return signature;
        } catch (const SourceError&) { /* different name layout */ }
    }
    return builtinCallSignature(spec, call, "surface.paint");
}
CallSignature partCallSignature(std::span<const PartInput> inputs) {
    std::vector<std::string> names;
    for (const auto& input : inputs) names.push_back(input.name);
    auto result = ordered(partParameters(names));
    result.effects = findOpcode(Opcode::PartDraw)->effects;
    result.evaluationOrder.clear();
    for (std::size_t i = 0; i < inputs.size(); ++i) {
        result.evaluationOrder.push_back(kPartFirstInput + i);
        result.defaults[kPartFirstInput + i] = inputs[i].declaration;
    }
    for (std::size_t i = 0; i < kPartFirstInput; ++i) result.evaluationOrder.push_back(i);
    for (std::size_t i = kPartTransformBegin; i < kPartTransformEnd; ++i) result.defaults[i] = i32{0};
    return result;
}
CallSignature skinCallSignature(std::span<const PartInput> inputs) {
    std::vector<std::string> names;
    for (const auto& input : inputs) names.push_back(input.name);
    auto result = ordered(skinParameters(names));
    // Generating a texture runs an isolated stream and changes the model; it
    // may fail in the imported program or on its size.
    result.effects = {RngEffect::IsolatedCallee, true};
    result.effects.checks = static_cast<u16>(EffectCheck::Callee) | static_cast<u16>(EffectCheck::NumericRange);
    result.evaluationOrder.clear();
    for (std::size_t i = 0; i < inputs.size(); ++i) {
        result.evaluationOrder.push_back(kSkinFirstInput + i);
        result.defaults[kSkinFirstInput + i] = inputs[i].declaration;
    }
    result.evaluationOrder.push_back(kSkinSeedSlot);
    result.evaluationOrder.push_back(kSkinSizeSlot);
    return result;
}
// `alias.render(seed, inputs...)` (M3): a part builds in its own stream and
// changes the model, like a 2D part draws.
CallSignature modelPartCallSignature(std::span<const PartInput> inputs) {
    std::vector<std::string> names;
    for (const auto& input : inputs) names.push_back(input.name);
    auto result = ordered(modelPartParameters(names));
    result.effects = findOpcode(Opcode::PartDraw)->effects;
    result.evaluationOrder.clear();
    for (std::size_t i = 0; i < inputs.size(); ++i) {
        result.evaluationOrder.push_back(kModelPartFirstInput + i);
        result.defaults[kModelPartFirstInput + i] = inputs[i].declaration;
    }
    result.evaluationOrder.push_back(kModelPartSeedSlot);
    return result;
}
BoundCallPlan bindCall(const Call& call, CallSignature signature, std::string_view function) {
    auto arguments = bindArguments(call, signature.parameters, function);
    return {std::move(signature), std::move(arguments)};
}

std::optional<BoundCallPlan> sourceCallPlan(const Program& program, const Call& call,
    const std::map<std::string, const Program*>& parts) {
    const auto name = calleeName(*call.callee);
    if (name == "surface.paint") {
        // Without imported types, try the layer overloads first: after the
        // image they accept only a static blend mode. Mask painting takes a
        // color there instead.
        try {
            auto layer = bindCall(call, surfacePaintCallSignature(call, ValueType::Layer), name);
            const Arg* blend = layer.arguments.size() > 1 ? layer.arguments[1] : nullptr;
            if (!blend || blend->name || surfaceMode(*blend->value)) return layer;
        } catch (const SourceError&) { /* no layer overload binds these arguments */ }
        return bindCall(call, surfacePaintCallSignature(call, ValueType::Mask), name);
    }
    const bool model = isModel(program.asset);
    if (const auto* found = findBuiltin(name, model))
        return bindCall(call, builtinCallSignature(*found, call, name), name);
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
            return bindCall(call, model ? skinCallSignature(inputs) : partCallSignature(inputs), name);
        }
    }
    if (model && name.ends_with(".render")) {
        const auto found = parts.find(name.substr(0, name.size() - 7));
        if (found != parts.end() && isModel(found->second->asset)) {
            std::vector<PartInput> inputs;
            for (const auto& input : found->second->asset.inputs)
                inputs.push_back({input.type, input.name, &input});
            return bindCall(call, modelPartCallSignature(inputs), name);
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
