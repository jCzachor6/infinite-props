#pragma once

#include "iprop/Error.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

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

// A distribution description (spec v0.5 §4.1): a family and its binary32
// parameters. None is the typed default of a slot that was never assigned.
enum class DistributionFamily : u8 { None = 0, Gaussian = 1, Triangular = 2, Beta = 3, Weighted = 4, Density = 5 };
// Keep each numeric operand's original type and bits. In particular, adjacent
// i32 keys above 2^24 must never pass through binary32.
using DistributionNumber = std::variant<i32, f32>;
struct DistributionPoint {
    DistributionNumber key, value;
};
// Every point distribution, whether pooled, built or constant, holds at most
// this many points.
inline constexpr std::size_t kMaxDistributionPoints = 256;
struct PointDistributionData {
    DistributionFamily family = DistributionFamily::None;
    std::vector<DistributionPoint> points;
};
struct Distribution {
    DistributionFamily family = DistributionFamily::None;
    // gaussian: mean, sigma; triangular: min, peak, max; beta: alpha, beta, min, max
    std::array<f32, 4> parameters{};
    std::shared_ptr<const PointDistributionData> points;

    bool operator==(const Distribution&) const = default;
};

} // namespace iprop
