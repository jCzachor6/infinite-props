#include "CallResolver.hpp"
#include "ips/BuiltinArguments.hpp"
#include "Plan.hpp"
#include "ips/Fold.hpp"
#include "ips/PartTransform.hpp"

namespace iprop::compiler {
namespace {
std::string calleeName(const ips::Expr& expression) {
    if (const auto* id = std::get_if<ips::Identifier>(&expression)) return id->name;
    if (const auto* member = std::get_if<ips::Member>(&expression))
        return calleeName(*member->object) + "." + member->name;
    throw ips::SourceError{ips::locationOf(expression), "cannot call this expression"};
}
}

PlannedArgument CallResolver::argument(const ips::Expr& expression, ips::ValueType expected) const {
    return {&expression, expected, expected == ips::ValueType::F32 &&
        function_.typeOf(expression) == ips::ValueType::I32};
}

ResolvedCall CallResolver::resolve(const ips::Call& call) const {
    using T = ips::ValueType;
    const auto name = calleeName(*call.callee);
    const auto& bound = function_.boundCalls.at(&call);
    const auto& args = bound.arguments;
    if (name == "ramp") {
        RampCall result{&plan_.palette(*function_.owner, *args[0]->value),
            argument(*args[1]->value, T::F32), {}};
        if (const auto& value = function_.expression(*result.position.expression).constant; value) {
            const f32 t = result.position.widen ? static_cast<f32>(std::get<i32>(*value)) : std::get<f32>(*value);
            result.constant = ips::sampleRamp(*result.palette, t);
        }
        return {call.location, std::move(result)};
    }
    const auto ns = functionNamespace(function_.name);
    if (const auto* found = ips::findBuiltin(name, plan_.model)) {
        const auto& spec = *found;
        std::vector<T> types;
        for (const auto* arg : args) types.push_back(function_.typeOf(*arg->value));
        if (spec.lowering == ips::BuiltinLowering::Pick || spec.lowering == ips::BuiltinLowering::Choose) {
            const std::size_t first = spec.lowering == ips::BuiltinLowering::Choose ? 1 : 0;
            const auto type = ips::selectionType(std::span(types).subspan(first));
            if (!type) throw ips::SourceError{call.location, "invalid choices"};
            SelectionCall result{*type, {}};
            for (const auto slot : bound.signature.evaluationOrder) {
                if (first && slot == 0) result.seed = argument(*args[slot]->value, T::I32);
                else result.choices.push_back(argument(*args[slot]->value, *type));
            }
            return {call.location, std::move(result)};
        }
        BuiltinCall result{&spec, ips::builtinOverload(spec, types), {}};
        if (ips::variadicPolygon(spec, args.size())) {
            for (const auto i : bound.signature.evaluationOrder)
                result.arguments.push_back(argument(*args[i]->value, i + 1 == args.size() ? T::Color : T::F32));
        } else {
            if (!result.overload) throw ips::SourceError{call.location,"invalid builtin overload"};
            for (const auto i : bound.signature.evaluationOrder)
                result.arguments.push_back(argument(*args[i]->value, result.overload->params[i].type));
        }
        return {call.location, std::move(result)};
    }
    if (std::holds_alternative<ips::Member>(*call.callee)) {
        // Imports are named by their path from the model, as a part's
        // functions are: `oak` in part `leg` is the skin `leg.oak`.
        const std::string alias = ns + name.substr(0, name.find('.'));
        if (plan_.model && name.ends_with(".render")) return {call.location, modelPart(call, alias)};
        if (plan_.model) return {call.location, skin(call, alias)};
        return {call.location, part(call, alias)};
    }
    const auto id = plan_.functionId(name, ns);
    if (!id) throw ips::SourceError{call.location, "unknown function '" + name + "'"};
    const auto& callee = plan_.functions[*id];
    UserCall result{*id, callee.langResult.value_or(T::Void), {}};
    for (const auto i : bound.signature.evaluationOrder)
        result.arguments.push_back(argument(*args[i]->value, ips::valueTypeOf(callee.ast->params[i].type)));
    return {call.location, std::move(result)};
}

SkinCall CallResolver::skin(const ips::Call& call, const std::string& alias) const {
    SkinCall result{};
    result.program = plan_.skinIndex.at(alias);
    const auto& bound = function_.boundCalls.at(&call);
    const auto& inputs = plan_.skins[result.program].program->asset.inputs;
    const auto& args = bound.arguments;
    for (std::size_t i = 0; i < inputs.size(); ++i) {
        const std::size_t slot = ips::kSkinFirstInput + i;
        if (const auto* supplied = args[slot])
            result.inputs.emplace_back(argument(*supplied->value, ips::valueTypeOf(inputs[i].type)));
        else result.inputs.emplace_back(plan_.inputDefaults.at(&inputs[i]).value);
    }
    result.seed = argument(*args[ips::kSkinSeedSlot]->value, ips::ValueType::I32);
    if (const auto* size = args[ips::kSkinSizeSlot]) result.size = size->value.get();
    return result;
}

ModelPartCall CallResolver::modelPart(const ips::Call& call, const std::string& path) const {
    ModelPartCall result{};
    result.part = plan_.partIndex.at(path);
    const auto& bound = function_.boundCalls.at(&call);
    const auto& inputs = plan_.parts[result.part]->asset.inputs;
    const auto& args = bound.arguments;
    for (std::size_t i = 0; i < inputs.size(); ++i) {
        if (const auto* supplied = args[ips::kModelPartFirstInput + i])
            result.inputs.emplace_back(argument(*supplied->value, ips::valueTypeOf(inputs[i].type)));
        else result.inputs.emplace_back(plan_.inputDefaults.at(&inputs[i]).value);
    }
    result.seed = argument(*args[ips::kModelPartSeedSlot]->value, ips::ValueType::I32);
    return result;
}

PartCall CallResolver::part(const ips::Call& call, const std::string& path) const {
    PartCall result{};
    result.part = plan_.partIndex.at(path);
    const auto& bound = function_.boundCalls.at(&call);
    result.transform = ips::resolvePartTransform(bound);
    const auto& inputs = plan_.parts[result.part]->asset.inputs;
    const auto& args = bound.arguments;
    for (const auto slot : bound.signature.evaluationOrder) {
        if (slot >= ips::kPartTransformBegin && slot < ips::kPartTransformEnd) continue; // compile-time transforms
        if (slot == ips::kPartAtSlot) {
            result.arguments.emplace_back(PartCall::Placement{args[slot]->value.get()});
        } else if (const auto* supplied = args[slot]) {
            const auto expected = slot == ips::kPartSeedSlot ? ips::ValueType::I32 :
                ips::valueTypeOf(inputs[slot - ips::kPartFirstInput].type);
            result.arguments.emplace_back(argument(*supplied->value, expected));
        } else {
            const auto* declaration = std::get<const ips::InputDecl*>(bound.signature.defaults[slot]);
            result.arguments.emplace_back(plan_.inputDefaults.at(declaration).value);
        }
    }
    return result;
}

} // namespace iprop::compiler
