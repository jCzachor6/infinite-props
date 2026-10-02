#include "iprop/math/Color.hpp"
#include "iprop/math/Scalar.hpp"
#include "iprop/math/MinMax.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace iprop::math {

Color colorMix(Color a, Color b, f32 t) {
    t = f32MinMax(f32MinMax(t, 0.f, true), 1.f, false);
    // Explicit fused multiply-add: the QuantSpace reference builds
    // contract this expression, so the pinned formula reproduces its
    // exact output bit for bit (spec §12.6). std::fma is deterministic.
    const auto channel = [t](u8 x, u8 y) {
        return static_cast<u8>(std::fma(static_cast<f32>(x), 1.f - t,
                                        static_cast<f32>(y) * t));
    };
    return Color{channel(a.r, b.r), channel(a.g, b.g), channel(a.b, b.b), a.a};
}

Color colorShade(Color c, i32 amount) {
    const auto channel = [amount](u8 x) {
        return static_cast<u8>(std::clamp(static_cast<i64>(x) + amount, i64{0}, i64{255}));
    };
    return Color{channel(c.r), channel(c.g), channel(c.b), c.a};
}

Color colorSetA(Color c, i32 amount) {
    return Color{c.r, c.g, c.b,
                        static_cast<u8>(std::clamp(amount, 0, 255))};
}

Color colorMul(Color a, Color b) {
    return Color{static_cast<u8>((static_cast<u32>(a.r) * b.r) / 255u),
                        static_cast<u8>((static_cast<u32>(a.g) * b.g) / 255u),
                        static_cast<u8>((static_cast<u32>(a.b) * b.b) / 255u),
                        a.a};
}

Color colorScale(Color c, f32 s) {
    const auto channel = [s](u8 ch) {
        const i32 truncated = f32ToI32(static_cast<f32>(ch) * s);
        return static_cast<u8>(std::clamp(truncated, 0, 255));
    };
    return Color{channel(c.r), channel(c.g), channel(c.b), c.a};
}

Color colorHsv(const Color& c, f32 hueShift, f32 satScale, f32 valScale) {
    const f32 r = static_cast<f32>(c.r) / 255.f;
    const f32 g = static_cast<f32>(c.g) / 255.f;
    const f32 b = static_cast<f32>(c.b) / 255.f;
    const f32 mx = std::fmax(std::fmax(r, g), b);
    const f32 mn = std::fmin(std::fmin(r, g), b);
    const f32 delta = mx - mn;
    const f32 v = mx;
    const f32 s = mx == 0.f ? 0.f : delta / mx;
    f32 h;
    if (delta == 0.f) {
        h = 0.f;
    } else if (mx == r) {
        // The 6 * delta is the five pinned additions.
        const f32 d6 = ((((delta + delta) + delta) + delta) + delta) + delta;
        h = (g - b) / d6;
        h = h - std::floor(h); // the negative wrap
    } else if (mx == g) {
        const f32 d6 = ((((delta + delta) + delta) + delta) + delta) + delta;
        h = (b - r) / d6 + 1.f / 3.f;
    } else {
        const f32 d6 = ((((delta + delta) + delta) + delta) + delta) + delta;
        h = (r - g) / d6 + 2.f / 3.f;
    }
    h = h + hueShift;
    h = h - std::floor(h);
    const f32 s2 = std::clamp(s * satScale, 0.f, 1.f);
    const f32 v2 = std::clamp(v * valScale, 0.f, 1.f);
    const f32 chroma = v2 * s2;
    const f32 h6 = h * 6.f;
    const f32 sector = std::fmod(std::floor(h6), 6.f);
    // Keep the fractional hue for interpolation; only sector selection floors it.
    const f32 parity = std::fmod(h6, 2.f);
    const f32 x = chroma * (1.f - std::fabs(parity - 1.f));
    const f32 m = v2 - chroma;
    f32 r1;
    f32 g1;
    f32 b1;
    switch (static_cast<int>(sector)) {
    case 0: r1 = chroma; g1 = x; b1 = 0.f; break;
    case 1: r1 = x; g1 = chroma; b1 = 0.f; break;
    case 2: r1 = 0.f; g1 = chroma; b1 = x; break;
    case 3: r1 = 0.f; g1 = x; b1 = chroma; break;
    case 4: r1 = x; g1 = 0.f; b1 = chroma; break;
    default: r1 = chroma; g1 = 0.f; b1 = x; break;
    }
    const auto toByte = [](f32 channel) {
        const i32 truncated = f32ToI32(channel * 255.f);
        return static_cast<u8>(std::clamp(truncated, 0, 255));
    };
    return Color{toByte(r1 + m), toByte(g1 + m), toByte(b1 + m), c.a};
}

f32 ditherThreshold(f32 t, i32 x, i32 y) {
    static constexpr u8 bayer[4][4] = {
        {0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};
    const u32 i = static_cast<u32>(x) & 3u;
    const u32 j = static_cast<u32>(y) & 3u;
    const f32 threshold = (static_cast<f32>(bayer[j][i]) + 0.5f) / 16.f;
    return t >= threshold ? 1.f : 0.f;
}

} // namespace iprop::math
