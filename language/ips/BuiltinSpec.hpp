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
#include <variant>

namespace iprop::ips {

enum class BuiltinLowering { Opcode, Round, Polygon, ColorConstructor, Pick, Chance, Roll, Choose, Beta, ListLength, SurfacePaint };
struct BuiltinParameter {
    std::string name;
    ValueType type;
};
enum class BuiltinAdaptation { Direct, Coordinates, IntegerCoordinates, VectorAngle, VectorLerp };
struct BuiltinOverload {
    std::vector<BuiltinParameter> params;
    std::optional<Opcode> opcode;
    ValueType result = ValueType::Void;
    BuiltinAdaptation adaptation = BuiltinAdaptation::Direct;
};
// Argument reads refer to already evaluated values. Code emission and asset
// constants share this expansion into existing VM operations.
using BuiltinStep = std::variant<std::size_t, Opcode>;
std::vector<BuiltinStep> builtinExpansion(const BuiltinOverload& overload);
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
// Receiver-first signatures for the statically resolved sampler method.
// Kept separate from global builtin pick and shared by typing/docs/lowering.
const BuiltinSpec& samplerPickSpec();
const BuiltinSpec& stepperPickSpec();
const std::map<std::string, BuiltinSpec>& listMethodSpecs();
const std::map<std::string, BuiltinSpec>& surfaceMethodSpecs();
const BuiltinOverload* builtinOverload(const BuiltinSpec& spec, std::size_t arity);
const BuiltinOverload* builtinOverload(const BuiltinSpec& spec, std::span<const ValueType> arguments);
// A polygon call without a fixed overload of its arity: poly(color, x0, y0, ...)
// is variadic, while poly(points, color) lowers through its overload.
bool variadicPolygon(const BuiltinSpec& spec, std::size_t arity);
// The distribution constructors arrived after helpers could take their
// names, so a helper declared with one of these names keeps its calls.
bool builtinYieldsToHelper(std::string_view name);
// Choices preserve identical storable types; mixed numeric choices widen to f32.
std::optional<ValueType> selectionType(std::span<const ValueType> arguments);

// Fixed overload effects come from their opcode contract. Variadic/special
// lowering effects are attached to the lowering, never a parallel name table.
OperationEffects builtinEffects(const BuiltinSpec& spec, const BuiltinOverload* overload = nullptr);

} // namespace iprop::ips
