#pragma once

#include "ips/Ast.hpp"
#include "ips/BuiltinSpec.hpp"

#include <string_view>

namespace iprop::ips {

// Maps positional/named arguments into signature order. Rejects duplicate,
// missing and unknown names before typing or emission can observe a call.
std::vector<const Arg*> bindArguments(const Call& call, std::span<const std::string> names,
                                     std::string_view function);
std::vector<const Arg*> builtinArguments(const Call& call, const BuiltinSpec& spec,
                                        std::string_view function);

// The same fixed/variadic names used by call binding and language description.
std::vector<std::string> builtinParameterNames(const BuiltinSpec& spec, std::size_t arity,
                                              const SourceLocation& at, std::string_view function);
std::span<const std::string> rampParameterNames();

} // namespace iprop::ips
