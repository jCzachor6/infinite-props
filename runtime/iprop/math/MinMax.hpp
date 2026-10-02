#pragma once

#include "iprop/Types.hpp"
#include "iprop/math/Float.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace iprop::math {

// Pinned minNum/maxNum, including NaNs and signed zero (spec §12.8).
inline f32 f32MinMax(f32 a, f32 b, bool maximum) {
    if (isNaN(a)) {
        return isNaN(b) ? std::numeric_limits<f32>::quiet_NaN() : b;
    }
    if (isNaN(b)) {
        return a;
    }
    if (a == 0.0f && b == 0.0f) {
        const bool negative = maximum ? std::signbit(a) && std::signbit(b)
                                      : std::signbit(a) || std::signbit(b);
        return std::copysign(0.0f, negative ? -1.0f : 1.0f);
    }
    return maximum ? std::max(a, b) : std::min(a, b);
}

} // namespace iprop::math
