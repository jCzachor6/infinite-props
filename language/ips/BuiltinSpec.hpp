#pragma once

#include "iprop/Constants.hpp"
#include "iprop/OpcodeSpec.hpp"
#include "ips/Types.hpp"

#include <map>
#include <optional>
#include <span>
#include <string_view>
#include <string>
#include <utility>
#include <vector>

namespace iprop::ips {

enum class BuiltinLowering { Opcode, Round, Polygon, ColorConstructor, Pick, Chance, Roll, Choose, Beta };
struct BuiltinParameter {
    std::string name;
    ValueType type;
};
struct BuiltinOverload {
    std::vector<BuiltinParameter> params;
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
// The distribution constructors arrived after helpers could take their
// names, so a helper declared with one of these names keeps its calls.
bool builtinYieldsToHelper(std::string_view name);
// Choices preserve identical storable types; mixed numeric choices widen to f32.
std::optional<ValueType> selectionType(std::span<const ValueType> arguments);

// Fixed overload effects come from their opcode contract. Variadic/special
// lowering effects are attached to the lowering, never a parallel name table.
OperationEffects builtinEffects(const BuiltinSpec& spec, const BuiltinOverload* overload = nullptr);

} // namespace iprop::ips
