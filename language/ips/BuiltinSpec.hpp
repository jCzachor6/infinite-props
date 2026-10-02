#pragma once

#include "iprop/Constants.hpp"
#include "ips/Types.hpp"

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace iprop::ips {

enum class BuiltinLowering { Opcode, Round, Polygon };
struct BuiltinOverload {
    std::vector<ValueType> params;
    std::optional<Opcode> opcode;
};
struct BuiltinSpec {
    ValueType result;
    BuiltinLowering lowering;
    std::vector<BuiltinOverload> overloads;
};

// Language contracts are distinct from VM stack contracts: coercions, overloads
// and special lowerings belong here, without duplicating them in the emitter.
const std::map<std::string, BuiltinSpec>& builtinSpecs();
const BuiltinOverload* builtinOverload(const BuiltinSpec& spec, std::size_t arity);

} // namespace iprop::ips
