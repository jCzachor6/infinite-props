#pragma once

#include "ips/Ast.hpp"
#include "ips/BuiltinSpec.hpp"

#include <string_view>

namespace iprop::ips {

struct ArgumentParameter {
    std::string name;
    bool required = true;
    bool positional = true;
};

// The generic binder also represents optional/defaulted and named-only slots.
// Omitted optional parameters remain null; consumers supply their typed
// default. Positional arguments retain the legacy ordinal mapping, so naming
// a slot and also reaching it positionally is a duplicate, never an overwrite.
std::vector<const Arg*> bindArguments(const Call& call,
    std::span<const ArgumentParameter> parameters, std::string_view function);

// Maps positional/named arguments into signature order. Rejects duplicate,
// missing and unknown names before typing or emission can observe a call.
std::vector<const Arg*> bindArguments(const Call& call, std::span<const std::string> names,
                                     std::string_view function);

// The same fixed/variadic names used by call binding and language description.
std::vector<std::string> builtinParameterNames(const BuiltinSpec& spec, std::size_t arity,
                                              const SourceLocation& at, std::string_view function);
std::span<const std::string> rampParameterNames();

// Part controls use the existing seed/v/a names. Placement, transforms and
// uniform overrides are named-only; transforms/uniforms are optional.
// Result indices: seed,v,a,at,rot,mirror_x,mirror_y, then declared uniforms.
std::vector<ArgumentParameter> partParameters(std::span<const std::string> uniforms);
std::vector<const Arg*> partArguments(const Call& call,
    std::span<const std::string> uniforms, std::string_view function);

} // namespace iprop::ips
