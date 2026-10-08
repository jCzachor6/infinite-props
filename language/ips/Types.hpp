#pragma once

// The .ips semantic type system. Mirrors the VM's value types (spec §3/§9):
// i32, f32 and colour are storable scalars; point/rect/circle are compound
// values that only flow from vector literals and part-output reads into
// output writes and part-call placement — they cannot be stored in locals
// (the VM's out.*/part.* instructions consume stack values directly).

#include "ips/Ast.hpp"

#include <optional>
#include <string_view>

namespace iprop::ips {

enum class ValueType {
    I32,
    F32,
    Color,
    Vec2,
    Distribution,
    Rect,
    Circle,
    Void, // no value (canvas operations, part calls)
};

const char* typeName(ValueType type);

bool isNumeric(ValueType type);

// Locals can hold scalars, vec2 and distributions.
bool isStorable(ValueType type);

// Numeric mixing: identical types stay, mixed types promote to F32.
// Precondition: both operands are numeric.
ValueType promote(ValueType a, ValueType b);

// Can an argument of type `arg` be passed to a parameter of type `param`?
// i32 widens to f32 (the compiler inserts i32.to.f32); everything else must
// match exactly.
bool argFits(ValueType param, ValueType arg);

// The channel read by `c.r`, `c.g`, `c.b` or `c.a`: 0-3 in RGBA order.
std::optional<unsigned> colorChannel(std::string_view member);

// The ValueType of a declared parameter/return type (ParamType in Ast.hpp).
ValueType valueTypeOf(ParamType type);
ValueType valueTypeOf(InputType type);

} // namespace iprop::ips
