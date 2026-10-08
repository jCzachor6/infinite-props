#pragma once

#include "iprop/Types.hpp"

#include <variant>

namespace iprop {

// A typed operand-stack value (spec §3: i32, f32, colour, vec2; v0.5
// distribution). u32 values travel in i32 slots; conversions follow the
// pinned spec rules when needed.
using Value = std::variant<i32, f32, Color, Vec2, Distribution>;

} // namespace iprop
