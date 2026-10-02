#pragma once

#include "test_support.hpp"

#include <cmath>
#include <limits>

namespace math_test {
using iprop::f32;
using iprop::i32;
using iprop::u32;

constexpr i32 kMin = std::numeric_limits<i32>::min();
constexpr i32 kMax = std::numeric_limits<i32>::max();

inline f32 unitFromBits(u32 bits) {
    return static_cast<f32>(bits >> 8) * 0x1p-24f;
}

inline u32 ulp(f32 a, f32 b) {
    if (a == b || (std::isnan(a) && std::isnan(b))) {
        return 0;
    }
    const auto ordered = [](f32 value) {
        const u32 bits = test_support::bitsOf(value);
        return (bits & 0x80000000u) != 0 ? ~bits : bits | 0x80000000u;
    };
    const u32 x = ordered(a);
    const u32 y = ordered(b);
    return x > y ? x - y : y - x;
}

} // namespace math_test
