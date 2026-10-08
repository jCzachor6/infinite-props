#pragma once

#include "iprop/Error.hpp"

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>

namespace iprop {

// Fixed-width aliases (spec §3).
using u8 = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;
using i8 = std::int8_t;
using i16 = std::int16_t;
using i32 = std::int32_t;
using i64 = std::int64_t;

// f32 is IEEE-754 binary32 (spec §3).
using f32 = float;
static_assert(sizeof(f32) == 4, "f32 must be IEEE-754 binary32");

// RGBA8 colour (spec §3). A = 0 transparent, 255 opaque.
struct Color {
    u8 r = 0;
    u8 g = 0;
    u8 b = 0;
    u8 a = 0;

    constexpr bool operator==(const Color&) const = default;
};

// 2D vector in asset units (spec §0: y grows downward).
struct Vec2 {
    f32 x = 0.f;
    f32 y = 0.f;

    constexpr bool operator==(const Vec2&) const = default;
};

} // namespace iprop
