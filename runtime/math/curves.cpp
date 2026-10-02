#include "iprop/math/Curves.hpp"
#include "iprop/math/MinMax.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace iprop::math {

f32 gainCurve(f32 t, f32 k) {
    if (std::isnan(t) || std::isnan(k)) {
        return std::numeric_limits<f32>::quiet_NaN();
    }
    t = std::clamp(t, 0.f, 1.f);
    k = std::clamp(k, 1e-4f, 1.f);
    // Preserve the endpoints and the k -> 1 limit without evaluating the
    // singular bias at q = 0 or q = 1. Interior values converge to 0.5.
    if (t == 0.f) return 0.f;
    if (t == 1.f) return 1.f;
    if (k == 1.f) return 0.5f;
    // The Schlick bias/gain: g = t < 0.5 ? bias(2t, k)/2
    //                                   : bias(2t-1, 1-k)/2 + 0.5.
    const auto bias = [](f32 x, f32 q) {
        const f32 a = 1.f / q;
        const f32 b = a - 2.f;
        const f32 c = b * (1.f - x);
        const f32 d = c + 1.f;
        return x / d;
    };
    if (t < 0.5f) {
        return bias(t + t, k) * 0.5f;
    }
    return bias(t + t - 1.f, 1.f - k) * 0.5f + 0.5f;
}

f32 quinticCurve(f32 t) {
    t = std::clamp(t, 0.f, 1.f);
    return quinticPolynomial(t);
}

// Share the pinned math package's NaN and signed-zero ordering.
f32 clampValue(f32 t, f32 a, f32 b) {
    const f32 lo = f32MinMax(a, b, false);
    const f32 hi = f32MinMax(a, b, true);
    return f32MinMax(f32MinMax(t, lo, true), hi, false);
}

f32 smoothstepEdge(f32 a, f32 b, f32 t) {
    const f32 d = b - a;
    const f32 u = d == 0.f ? 0.f : clampValue((t - a) / d, 0.f, 1.f);
    return u * u * (3.f - 2.f * u);
}

f32 remapValue(f32 t, f32 a, f32 b, f32 c, f32 d) {
    const f32 span = b - a;
    const f32 u = span == 0.f ? 0.f : clampValue((t - a) / span, 0.f, 1.f);
    return c + u * (d - c);
}

f32 lerp(f32 a, f32 b, f32 t) {
    t = std::clamp(t, 0.f, 1.f);
    return a * (1.f - t) + b * t;
}

} // namespace iprop::math
