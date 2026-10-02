#include "CallResolver.hpp"
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
        return {call.location, RampCall{&plan_.palette(*function_.owner, *call.args[0].value),
            argument(*call.args[1].value, T::F32)}};
    }
    if (const auto found = ips::builtinSpecs().find(name); found != ips::builtinSpecs().end()) {
        const auto& spec = found->second;
        BuiltinCall result{&spec, ips::builtinOverload(spec, call.args.size()), {}};
        if (spec.lowering == ips::BuiltinLowering::Polygon) {
            for (std::size_t i = 1; i < call.args.size(); ++i)
                result.arguments.push_back(argument(*call.args[i].value, T::F32));
            result.arguments.push_back(argument(*call.args[0].value, T::Color));
        } else {
            if (!result.overload) throw ips::SourceError{call.location,"invalid builtin overload"};
            for (std::size_t i = 0; i < call.args.size(); ++i)
                result.arguments.push_back(argument(*call.args[i].value, result.overload->params[i]));
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
    for (std::size_t i = 0; i < call.args.size(); ++i)
        result.arguments.push_back(argument(*call.args[i].value, ips::valueTypeOf(callee.ast->params[i].type)));
    return {call.location, std::move(result)};
}

PartCall CallResolver::part(const ips::Call& call, const std::string& alias) const {
    PartCall result{};
    result.part = plan_.partIndex.at(alias);
    result.transform = ips::resolvePartTransform(call);
    std::map<std::string, const ips::Expr*> named;
    std::size_t positional = 0;
    for (const auto& arg : call.args) {
        if (arg.name) named.emplace(*arg.name, arg.value.get());
        else result.positional.at(positional++) = argument(*arg.value, ips::ValueType::I32);
    }
    result.at = named.at("at");
    for (const auto& input : plan_.parts[result.part]->asset.inputs) {
        if (const auto found = named.find(input.name); found != named.end()) {
            const auto expected = ips::valueTypeOf(input.type);
            result.uniforms.emplace_back(argument(*found->second, expected));
        } else {
            Value value;
            if (!std::holds_alternative<ips::Literal>(*input.defaultValue) ||
                !ips::foldExpression(*input.defaultValue, value))
                throw ips::SourceError{input.location, "part input '" + input.name + "' has no constant default",
                    plan_.sourcePath(plan_.parts[result.part])};
            result.uniforms.emplace_back(value);
        }
    }
    return result;
}

} // namespace iprop::compiler
