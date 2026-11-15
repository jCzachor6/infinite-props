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

enum class BuiltinLowering { Opcode, Round, Polygon, ColorConstructor, Pick, Chance, Roll, Choose, Beta, ListLength, SurfacePaint,
    NormalColor };
// A seed parameter takes an i32 or a seed (named seeds v1.1); both arrive as
// the same 32 bits. The role is part of the registry, never read off a name.
enum class ParameterRole : u8 { Value, Seed };
struct BuiltinParameter {
    std::string name;
    ValueType type;
    ParameterRole role = ParameterRole::Value;
};
// Whether a parameter takes an argument of this type, widening included.
bool parameterAccepts(const BuiltinParameter& parameter, ValueType argument);
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
// A builtin function called by a bare name, such as `rect` or `ramp`. A helper
// cannot take one of these names: its calls would resolve to the builtin.
bool isBuiltinFunctionName(std::string_view name);
// The first segment of a dotted builtin or builtin constant, such as `noise`
// or `surface`. An import alias cannot reuse one: `alias.draw(...)` and the
// namespace's own members would share one spelling.
bool isBuiltinNamespace(std::string_view name);
// color.normal(x, y, z): the colour that stores a direction given in asset
// coordinates (y downward), by the VM's own f32 operations in the order the
// emitter lowers them (spec normal maps v1.1). `flat` reports the fallback
// taken when the squared length is zero or not finite.
struct NormalColorValue { Color color; bool flat = false; };
NormalColorValue normalColorValue(f32 x, f32 y, f32 z);
// Named seeds (spec named seeds v1.1). FNV-1a 32 over bytes, pinned.
constexpr u32 fnv1a32(std::string_view bytes) {
    u32 h = 2166136261u;
    for (const char c : bytes) h = (h ^ static_cast<u8>(c)) * 16777619u;
    return h;
}
// The domain tags keep name keys and index keys apart: fork("12") is not fork(12).
inline constexpr u32 kForkTextTag = fnv1a32("iprop.seed.fork/text/v1");
inline constexpr u32 kForkIndexTag = fnv1a32("iprop.seed.fork/index/v1");
// What x.fork("label") mixes into x: hash32(kForkTextTag ^ fnv1a32(label)),
// computed while compiling by the VM's own hash instruction. The label is
// the decoded bytes of the string literal.
u32 forkLabelSalt(std::string_view label);
// Choices preserve identical storable types; mixed numeric choices widen to f32.
std::optional<ValueType> selectionType(std::span<const ValueType> arguments);

// Fixed overload effects come from their opcode contract. Variadic/special
// lowering effects are attached to the lowering, never a parallel name table.
OperationEffects builtinEffects(const BuiltinSpec& spec, const BuiltinOverload* overload = nullptr);

} // namespace iprop::ips
