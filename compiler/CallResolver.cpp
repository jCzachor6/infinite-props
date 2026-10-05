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
    if (name == "ramp") {
        const std::vector<std::string> names{"palette", "t"};
        const auto args = ips::bindArguments(call, names, name);
        RampCall result{&plan_.palette(*function_.owner, *args[0]->value),
            argument(*args[1]->value, T::F32), {}};
        if (const auto& value = function_.expression(*result.position.expression).constant; value) {
            const f32 t = result.position.widen ? static_cast<f32>(std::get<i32>(*value)) : std::get<f32>(*value);
            result.constant = ips::sampleRamp(*result.palette, t);
        }
        return {call.location, std::move(result)};
    }
    if (const auto found = ips::builtinSpecs().find(name); found != ips::builtinSpecs().end()) {
        const auto& spec = found->second;
        const auto args = ips::builtinArguments(call, spec, name);
        std::vector<T> types;
        for (const auto* arg : args) types.push_back(function_.typeOf(*arg->value));
        if ((spec.lowering == ips::BuiltinLowering::Pick && args.size() >= 2) ||
            spec.lowering == ips::BuiltinLowering::Choose) {
            const std::size_t first = spec.lowering == ips::BuiltinLowering::Choose ? 1 : 0;
            const auto type = ips::selectionType(std::span(types).subspan(first));
            if (!type) throw ips::SourceError{call.location, "invalid choices"};
            SelectionCall result{*type, {}};
            if (first) result.seed = argument(*args[0]->value, T::I32);
            for (std::size_t i = first; i < args.size(); ++i) result.choices.push_back(argument(*args[i]->value, *type));
            return {call.location, std::move(result)};
        }
        BuiltinCall result{&spec, ips::builtinOverload(spec, types), {}};
        if (spec.lowering == ips::BuiltinLowering::Polygon) {
            for (std::size_t i = 1; i < args.size(); ++i)
                result.arguments.push_back(argument(*args[i]->value, T::F32));
            result.arguments.push_back(argument(*args[0]->value, T::Color));
        } else {
            if (!result.overload) throw ips::SourceError{call.location,"invalid builtin overload"};
            for (std::size_t i = 0; i < args.size(); ++i)
                result.arguments.push_back(argument(*args[i]->value, result.overload->params[i].type));
        }
        return {call.location, std::move(result)};
    }
    if (std::holds_alternative<ips::Member>(*call.callee))
        return {call.location, part(call, name.substr(0, name.find('.')))};
    const auto dot = function_.name.find('.');
    const auto ns = dot == std::string::npos ? "" : function_.name.substr(0, dot + 1);
    const auto id = plan_.functionId(name, ns);
    if (!id) throw ips::SourceError{call.location, "unknown function '" + name + "'"};
    const auto& callee = plan_.functions[*id];
    UserCall result{*id, callee.langResult.value_or(T::Void), {}};
    const auto args = ips::bindArguments(call, ips::declaredParamNames(*callee.ast), name);
    for (std::size_t i = 0; i < args.size(); ++i)
        result.arguments.push_back(argument(*args[i]->value, ips::valueTypeOf(callee.ast->params[i].type)));
    return {call.location, std::move(result)};
}

PartCall CallResolver::part(const ips::Call& call, const std::string& alias) const {
    PartCall result{};
    result.part = plan_.partIndex.at(alias);
    result.transform = ips::resolvePartTransform(call);
    std::vector<std::string> uniforms;
    const auto& inputs = plan_.parts[result.part]->asset.inputs;
    for (const auto& input : inputs) uniforms.push_back(input.name);
    const auto args = ips::partArguments(call, uniforms, alias + ".draw");
    for (std::size_t i=0;i<3;++i)
        result.positional[i] = argument(*args[i]->value, ips::ValueType::I32);
    result.at = args[3]->value.get();
    for (std::size_t i=0;i<inputs.size();++i) {
        if (const auto* override = args[7+i]) {
            const auto expected = ips::valueTypeOf(inputs[i].type);
            result.uniforms.emplace_back(argument(*override->value, expected));
        } else {
            result.uniforms.emplace_back(plan_.inputDefaults.at(&inputs[i]).value);
        }
    }
    return result;
}

} // namespace iprop::compiler
