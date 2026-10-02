#pragma once

#include "iprop/Constants.hpp"
#include "ips/Types.hpp"

#include <map>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace iprop::ips {

enum class BuiltinLowering { Opcode, Round, Polygon, ColorConstructor };
struct BuiltinOverload {
    std::vector<ValueType> params;
    std::optional<Opcode> opcode;
    ValueType result = ValueType::Void;
};
struct BuiltinSpec {
    BuiltinLowering lowering;
    std::vector<BuiltinOverload> overloads;

    BuiltinSpec(ValueType result, BuiltinLowering lowering,
                std::vector<BuiltinOverload> overloads)
        : lowering(lowering), overloads(std::move(overloads)) {
        for (auto& overload : this->overloads) overload.result = result;
    }
    BuiltinSpec(BuiltinLowering lowering, std::vector<BuiltinOverload> overloads)
        : lowering(lowering), overloads(std::move(overloads)) {}
};

// Language contracts are distinct from VM stack contracts: coercions, overloads
// and special lowerings belong here, without duplicating them in the emitter.
const std::map<std::string, BuiltinSpec>& builtinSpecs();
const BuiltinOverload* builtinOverload(const BuiltinSpec& spec, std::size_t arity);
const BuiltinOverload* builtinOverload(const BuiltinSpec& spec, std::span<const ValueType> arguments);

} // namespace iprop::ips
