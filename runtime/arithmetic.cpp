#include "iprop/VmInternals.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace iprop::detail {
namespace {

// Portable arithmetic right shift with two's-complement semantics
// (spec §9.3; C++ right shift of negatives is implementation-defined).
i32 shiftRightSigned(i32 value, u32 shift) {
    if (value >= 0) {
        return static_cast<i32>(static_cast<u32>(value) >> shift);
    }
    return static_cast<i32>(~((~static_cast<u32>(value)) >> shift));
}

// f32 -> i32 per spec §9.4: truncate toward zero, NaN -> 0, infinities and
// out-of-range values saturate.
i32 f32ToI32(f32 value) {
    if (std::isnan(value)) {
        return 0;
    }
    if (value <= -2147483648.f) { // exact in binary32
        return std::numeric_limits<i32>::min();
    }
    if (value >= 2147483648.f) { // 2^31; larger values saturate
        return std::numeric_limits<i32>::max();
    }
    return static_cast<i32>(value); // truncates toward zero
}

// Spec §12.7: the pinned sine. Cody-Waite quadrant reduction and the Taylor
// polynomials on [-pi/4, pi/4], all in double precision. Every operation is
// a single IEEE round-to-nearest double op, so every backend reproduces the
// result bit for bit — this MUST NOT be replaced with the platform libm.
double sinCore(double x) {
    constexpr double PIO2 = 1.57079632679489661923e+00;    // pi/2 (double)
    constexpr double PIO2_HI = 1.57079632673412561417e+00; // 0x3FF921FB54400000
    constexpr double PIO2_LO = 6.07710050650619224932e-11; // 0x3DD0B4611A626331
    const double k = std::floor(x / PIO2 + 0.5); // the quadrant index
    const double r = (x - k * PIO2_HI) - k * PIO2_LO; // |r| <= pi/4

    // sin(r) and cos(r) in Horner form (the exact coefficients as written
    // in the spec; the compiler must not contract any mul+add).
    const double r2 = r * r;
    const double s =
        r * (1.0 + r2 * (-1.0 / 6.0 +
                         r2 * (1.0 / 120.0 +
                               r2 * (-1.0 / 5040.0 +
                                     r2 * (1.0 / 362880.0 +
                                           r2 * (-1.0 / 39916800.0))))));
    const double c =
        1.0 + r2 * (-0.5 +
                    r2 * (1.0 / 24.0 +
                          r2 * (-1.0 / 720.0 +
                                r2 * (1.0 / 40320.0 +
                                      r2 * (-1.0 / 3628800.0 +
                                            r2 * (1.0 / 479001600.0))))));

    // k mod 4, exact: the 0.25 scaling is a binary exponent shift and the
    // subtraction is exact for the small result.
    const double m = k - 4.0 * std::floor(k * 0.25);
    if (m == 0.0) {
        return s;
    }
    if (m == 1.0) {
        return c;
    }
    if (m == 2.0) {
        return -s;
    }
    return -c;
}

f32 f32Sin(f32 x) {
    const double value = static_cast<double>(x);
    if (std::isnan(value) || std::isinf(value)) {
        return std::numeric_limits<f32>::quiet_NaN();
    }
    return static_cast<f32>(sinCore(value));
}

// ---- the signed-distance fields (spec §12.9) ------------------------------

f32 distCircle(const Vec2& p, const Vec2& c, f32 r) {
    const f32 dx = p.x - c.x;
    const f32 dy = p.y - c.y;
    const f32 length = std::sqrt(dx * dx + dy * dy);
    return length - r;
}

f32 distRect(const Vec2& p, const Vec2& c, const Vec2& half) {
    const f32 dx = std::fabs(p.x - c.x) - half.x;
    const f32 dy = std::fabs(p.y - c.y) - half.y;
    const f32 qx = std::fmax(dx, 0.f);
    const f32 qy = std::fmax(dy, 0.f);
    const f32 outer = std::sqrt(qx * qx + qy * qy);
    const f32 inner = std::fmin(std::fmax(dx, dy), 0.f);
    return outer + inner;
}

f32 distRoundRect(const Vec2& p, const Vec2& c, const Vec2& half, f32 radius) {
    const f32 qx = std::fabs(p.x - c.x) - (half.x - radius);
    const f32 qy = std::fabs(p.y - c.y) - (half.y - radius);
    const f32 ox = std::fmax(qx, 0.f);
    const f32 oy = std::fmax(qy, 0.f);
    const f32 outer = std::sqrt(ox * ox + oy * oy);
    const f32 inner = std::fmin(std::fmax(qx, qy), 0.f);
    return outer + inner - radius;
}

f32 distLine(const Vec2& p, const Vec2& a, const Vec2& b) {
    const f32 bax = b.x - a.x;
    const f32 bay = b.y - a.y;
    const f32 pax = p.x - a.x;
    const f32 pay = p.y - a.y;
    const f32 dotPa = pax * bax + pay * bay;
    const f32 dotBa = bax * bax + bay * bay;
    const f32 t = dotPa / dotBa;
    const f32 h = std::fmin(std::fmax(t, 0.f), 1.f);
    const f32 qx = pax - bax * h;
    const f32 qy = pay - bay * h;
    return std::sqrt(qx * qx + qy * qy);
}

bool finitePoint(const Vec2& p) {
    return std::isfinite(p.x) && std::isfinite(p.y);
}

template <typename Point, typename Derivative, typename SecondDerivative>
f32 bezierDistance(const Vec2& p, const Vec2& startPoint, const Vec2& endPoint,
                   Point point, Derivative derivative, SecondDerivative secondDerivative) {
    f32 best = std::min(distCircle(p, startPoint, 0.f), distCircle(p, endPoint, 0.f));
    const f32 candidates[5] = {0.f, 0.25f, 0.5f, 0.75f, 1.f};
    for (const f32 start : candidates) {
        f32 t = start;
        for (int step = 0; step <= 4; ++step) {
            const Vec2 pt = point(t);
            const f32 rx = pt.x - p.x;
            const f32 ry = pt.y - p.y;
            const f32 distance = std::sqrt(rx * rx + ry * ry);
            best = std::min(best, distance);
            if (step == 4) {
                break;
            }
            const Vec2 dt = derivative(t);
            const Vec2 dd = secondDerivative(t);
            const f32 f = rx * dt.x + ry * dt.y;
            // Each dot product rounds separately, exactly as vec2.dot does.
            const f32 tangentDot = dt.x * dt.x + dt.y * dt.y;
            const f32 curvatureDot = rx * dd.x + ry * dd.y;
            const f32 fp = tangentDot + curvatureDot;
            if (fp == 0.f || !std::isfinite(fp) || !std::isfinite(f)) {
                break;
            }
            const f32 next = t - f / fp;
            if (!std::isfinite(next)) {
                break;
            }
            t = std::clamp(next, 0.f, 1.f);
        }
    }
    return best;
}

f32 quadraticBezierDistance(const Vec2& p, const Vec2& a, const Vec2& b, const Vec2& c) {
    if (!finitePoint(p) || !finitePoint(a) || !finitePoint(b) || !finitePoint(c)) {
        return std::numeric_limits<f32>::quiet_NaN();
    }
    if (a.x == b.x && b.x == c.x && a.y == b.y && b.y == c.y) {
        return distCircle(p, a, 0.f);
    }
    const auto point = [&](f32 t) -> Vec2 {
        if (t == 0.f) return a;
        if (t == 1.f) return c;
        const f32 s = 1.f - t;
        const f32 w0 = s * s;
        const f32 w1 = (s + s) * t;
        const f32 w2 = t * t;
        return {w0 * a.x + w1 * b.x + w2 * c.x,
                w0 * a.y + w1 * b.y + w2 * c.y};
    };
    const auto derivative = [&](f32 t) -> Vec2 {
        const f32 s = 1.f - t;
        const f32 w0 = s + s;
        const f32 w1 = t + t;
        return {w0 * (b.x - a.x) + w1 * (c.x - b.x),
                w0 * (b.y - a.y) + w1 * (c.y - b.y)};
    };
    const f32 ddx = a.x - (b.x + b.x) + c.x;
    const f32 ddy = a.y - (b.y + b.y) + c.y;
    const Vec2 dd{ddx + ddx, ddy + ddy};
    return bezierDistance(p, a, c, point, derivative, [&](f32) { return dd; });
}

f32 cubicBezierDistance(const Vec2& p, const Vec2& a, const Vec2& b,
                        const Vec2& c, const Vec2& d) {
    if (!finitePoint(p) || !finitePoint(a) || !finitePoint(b) ||
        !finitePoint(c) || !finitePoint(d)) {
        return std::numeric_limits<f32>::quiet_NaN();
    }
    if (a.x == b.x && b.x == c.x && c.x == d.x &&
        a.y == b.y && b.y == c.y && c.y == d.y) {
        return distCircle(p, a, 0.f);
    }
    const auto point = [&](f32 t) -> Vec2 {
        if (t == 0.f) return a;
        if (t == 1.f) return d;
        const f32 s = 1.f - t;
        const f32 w0 = s * s * s;
        const f32 w1 = 3.f * s * s * t;
        const f32 w2 = 3.f * s * t * t;
        const f32 w3 = t * t * t;
        return {w0 * a.x + w1 * b.x + w2 * c.x + w3 * d.x,
                w0 * a.y + w1 * b.y + w2 * c.y + w3 * d.y};
    };
    const auto derivative = [&](f32 t) -> Vec2 {
        const f32 s = 1.f - t;
        const f32 w0 = 3.f * s * s;
        const f32 w1 = 6.f * s * t;
        const f32 w2 = 3.f * t * t;
        return {w0 * (b.x - a.x) + w1 * (c.x - b.x) + w2 * (d.x - c.x),
                w0 * (b.y - a.y) + w1 * (c.y - b.y) + w2 * (d.y - c.y)};
    };
    const Vec2 ab{a.x - (b.x + b.x) + c.x, a.y - (b.y + b.y) + c.y};
    const Vec2 bc{b.x - (c.x + c.x) + d.x, b.y - (c.y + c.y) + d.y};
    const auto secondDerivative = [&](f32 t) -> Vec2 {
        const f32 w0 = 6.f * (1.f - t);
        const f32 w1 = 6.f * t;
        return {w0 * ab.x + w1 * bc.x, w0 * ab.y + w1 * bc.y};
    };
    return bezierDistance(p, a, d, point, derivative, secondDerivative);
}

f32 sdfFill(f32 d, f32 w) {
    const f32 t0 = d / w;
    const f32 t1 = t0 + 0.5f;
    const f32 t = std::fmin(std::fmax(t1, 0.f), 1.f);
    const f32 tt = t * t;
    const f32 s = 3.f - 2.f * t;
    return 1.f - tt * s;
}

// ---- the curves (spec §12.11) ----------------------------------------------

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
    const f32 t2 = t * t;
    const f32 t3 = t2 * t;
    const f32 inner = t * (t * 6.f - 15.f) + 10.f;
    return t3 * inner;
}

// ---- the colours (spec §12.12) ----------------------------------------------

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

// ---- the dither, the fold and the polar (spec §12.13) -----------------------

// The pinned trig helpers live further down; forward-declared for the fold.
f32 f32Cos(f32 x);
f32 f32Atan2(f32 y, f32 x);

f32 ditherThreshold(f32 t, i32 x, i32 y) {
    static constexpr u8 bayer[4][4] = {
        {0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};
    const u32 i = static_cast<u32>(x) & 3u;
    const u32 j = static_cast<u32>(y) & 3u;
    const f32 threshold = (static_cast<f32>(bayer[j][i]) + 0.5f) / 16.f;
    return t >= threshold ? 1.f : 0.f;
}

Vec2 vec2Fold(const Vec2& v, i32 n) {
    n = std::clamp(n, 1, 64);
    const f32 r = std::sqrt(v.x * v.x + v.y * v.y);
    const f32 a = f32Atan2(v.y, v.x);
    const f32 sector =
        6.28318530717958647692f / static_cast<f32>(n); // TWO_PI
    const f32 quotient = a / sector + 0.5f;
    const f32 folded = a - std::floor(quotient) * sector;
    const f32 magnitude = std::fabs(folded);
    const f32 x = r * f32Cos(magnitude);
    const f32 y = r * f32Sin(magnitude);
    // A multiply may choose either operand's NaN sign/payload on different
    // backends. Canonicalize each component after the final products.
    const f32 nan = std::numeric_limits<f32>::quiet_NaN();
    return Vec2{std::isnan(x) ? nan : x, std::isnan(y) ? nan : y};
}

Vec2 vec2Polar(const Vec2& v) {
    return Vec2{std::sqrt(v.x * v.x + v.y * v.y), f32Atan2(v.y, v.x)};
}

f32 f32Cos(f32 x) {
    const double value = static_cast<double>(x);
    if (std::isnan(value) || std::isinf(value)) {
        return std::numeric_limits<f32>::quiet_NaN();
    }
    constexpr double PIO2 = 1.57079632679489661923e+00;
    return static_cast<f32>(sinCore(value + PIO2));
}

// ---- the pinned math package (spec §12.8) --------------------------------

constexpr double kLn2 = 6.93147180559945309417e-01;
constexpr double kLog2e = 1.44269504088896340736e+00;
constexpr double kPi = 3.14159265358979323846e+00;
constexpr double kPiOver2 = 1.57079632679489661923e+00;

// exp2: n = floor(x), 2^x = e^(f*ln2) * 2^n with the Taylor series of
// degree 12 in Horner form. Guard nonfinite intermediates before converting
// the exponent to int; callers cast the result to f32.
double exp2Core(double x) {
    if (std::isnan(x)) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    if (std::isinf(x)) {
        return x > 0.0 ? std::numeric_limits<double>::infinity() : 0.0;
    }
    double n = std::floor(x);
    // Keep the exponent within the ldexp-safe range; the out-of-range
    // cases overflow/underflow to +/-inf / 0, exactly as the f32 cast
    // would.
    if (n > 2047.0) {
        return std::numeric_limits<double>::infinity();
    }
    if (n < -2047.0) {
        return 0.0;
    }
    const double f = x - n;
    const double t = f * kLn2;
    double e = 1.0 / 479001600.0; // 1/12!
    e = e * t + 1.0 / 39916800.0; // 1/11!
    e = e * t + 1.0 / 3628800.0;  // 1/10!
    e = e * t + 1.0 / 362880.0;   // 1/9!
    e = e * t + 1.0 / 40320.0;    // 1/8!
    e = e * t + 1.0 / 5040.0;     // 1/7!
    e = e * t + 1.0 / 720.0;      // 1/6!
    e = e * t + 1.0 / 120.0;      // 1/5!
    e = e * t + 1.0 / 24.0;       // 1/4!
    e = e * t + 1.0 / 6.0;        // 1/3!
    e = e * t + 0.5;              // 1/2!
    e = e * t + 1.0;
    e = e * t + 1.0;
    return std::ldexp(e, static_cast<int>(n));
}



// log2 for a finite positive argument: frexp + the atanh series of
// v = u/(u+2), u = m-1, m in [1,2). The caller handles the edge cases.
double log2Core(double x) {
    int exponent = 0;
    const double m = std::frexp(x, &exponent); // x = m * 2^e, m in [0.5, 1)
    const double mm = m < 1.0 ? m * 2.0 : m;  // mm in [1, 2)
    const int e = m < 1.0 ? exponent - 1 : exponent;
    const double u = mm - 1.0;                // u in [0, 1)
    const double v = u / (u + 2.0);           // v in [0, 1/3)
    const double v2 = v * v;
    double g = 1.0 / 15.0;
    g = g * v2 + 1.0 / 13.0;
    g = g * v2 + 1.0 / 11.0;
    g = g * v2 + 1.0 / 9.0;
    g = g * v2 + 1.0 / 7.0;
    g = g * v2 + 1.0 / 5.0;
    g = g * v2 + 1.0 / 3.0;
    g = g * v2 + 1.0;
    g = g * v;
    return static_cast<double>(e) + 2.0 * g / kLn2;
}

f32 f32Exp(f32 x) {
    const double value = static_cast<double>(x);
    if (std::isnan(value)) {
        return std::numeric_limits<f32>::quiet_NaN();
    }
    if (value == std::numeric_limits<double>::infinity()) {
        return std::numeric_limits<f32>::infinity();
    }
    if (value == -std::numeric_limits<double>::infinity()) {
        return 0.0f;
    }
    return static_cast<f32>(exp2Core(value * kLog2e));
}

// atan(t) for t in [0, 1]: one half-angle reduction plus the Taylor
// series of degree 17 in Horner form.
double atanCore(double t) {
    const double z = t / (1.0 + std::sqrt(1.0 + t * t));
    const double z2 = z * z;
    // atan(z) = z * (1 - z^2/3 + z^4/5 - ... + z^16/17)
    double a = 1.0 / 17.0;
    a = a * z2 - 1.0 / 15.0;
    a = a * z2 + 1.0 / 13.0;
    a = a * z2 - 1.0 / 11.0;
    a = a * z2 + 1.0 / 9.0;
    a = a * z2 - 1.0 / 7.0;
    a = a * z2 + 1.0 / 5.0;
    a = a * z2 - 1.0 / 3.0;
    a = a * z2 + 1.0;
    a = a * z;
    return 2.0 * a;
}

f32 f32Atan2(f32 y, f32 x) {
    const double yy = static_cast<double>(y);
    const double xx = static_cast<double>(x);
    if (std::isnan(yy) || std::isnan(xx)) {
        return std::numeric_limits<f32>::quiet_NaN();
    }
    if (yy == 0.0) {
        const bool xPos = xx > 0.0 || xx == 0.0;
        return static_cast<f32>(std::copysign(xPos ? 0.0 : kPi, yy));
    }
    if (std::isinf(yy)) {
        if (xx == std::numeric_limits<double>::infinity()) {
            return static_cast<f32>(std::copysign(kPi / 4.0, yy));
        }
        if (xx == -std::numeric_limits<double>::infinity()) {
            return static_cast<f32>(std::copysign(3.0 * kPi / 4.0, yy));
        }
        return static_cast<f32>(std::copysign(kPiOver2, yy));
    }
    if (xx == std::numeric_limits<double>::infinity()) {
        return static_cast<f32>(std::copysign(0.0, yy));
    }
    if (xx == -std::numeric_limits<double>::infinity()) {
        return static_cast<f32>(yy >= 0.0 ? kPi : -kPi);
    }
    if (xx == 0.0) {
        return static_cast<f32>(yy >= 0.0 ? kPiOver2 : -kPiOver2);
    }
    const double ax = std::fabs(xx);
    const double ay = std::fabs(yy);
    double r = atanCore(std::min(ax, ay) / std::max(ax, ay));
    if (ay > ax) {
        r = kPiOver2 - r;
    }
    if (yy < 0.0) {
        r = -r;
    }
    if (xx < 0.0) {
        r = (yy >= 0.0 ? kPi : -kPi) - r;
    }
    return static_cast<f32>(r);
}

f32 f32Pow(f32 x, f32 y) {
    const double a = static_cast<double>(x);
    const double b = static_cast<double>(y);
    if (b == 0.0) {
        return 1.0f; // even when x is NaN
    }
    if (a == 1.0) {
        return 1.0f;
    }
    if (std::isnan(a) || std::isnan(b)) {
        return std::numeric_limits<f32>::quiet_NaN();
    }
    if (std::isinf(b)) {
        const double magnitude = std::fabs(a);
        if (magnitude == 1.0) {
            return 1.0f;
        }
        return (magnitude > 1.0) == (b > 0.0)
                   ? std::numeric_limits<f32>::infinity()
                   : 0.0f;
    }
    const bool yInteger = std::isfinite(b) && b == std::floor(b);
    const bool yOdd = yInteger && std::fmod(std::floor(b), 2.0) != 0.0;
    if (std::isinf(a)) {
        const bool neg = std::signbit(a);
        double magnitude =
            b > 0.0 ? std::numeric_limits<double>::infinity() : 0.0;
        if (neg && yOdd) {
            magnitude = std::copysign(magnitude, -1.0);
        }
        return static_cast<f32>(magnitude);
    }
    if (a == 0.0) {
        const bool neg = std::signbit(a);
        double magnitude =
            b > 0.0 ? 0.0 : std::numeric_limits<double>::infinity();
        if (neg && yOdd) {
            magnitude = std::copysign(magnitude, -1.0);
        }
        return static_cast<f32>(magnitude);
    }
    if (a < 0.0 && !yInteger) {
        return std::numeric_limits<f32>::quiet_NaN();
    }
    double r = exp2Core(b * log2Core(std::fabs(a)));
    if (a < 0.0 && yOdd) {
        r = -r;
    }
    return static_cast<f32>(r);
}

f32 f32MinMax(f32 a, f32 b, bool maximum) {
    if (std::isnan(a)) {
        return std::isnan(b) ? std::numeric_limits<f32>::quiet_NaN() : b;
    }
    if (std::isnan(b)) {
        return a;
    }
    if (a == 0.0f && b == 0.0f) {
        const bool negative = maximum ? std::signbit(a) && std::signbit(b)
                                      : std::signbit(a) || std::signbit(b);
        return std::copysign(0.0f, negative ? -1.0f : 1.0f);
    }
    return maximum ? std::max(a, b) : std::min(a, b);
}

// Shared shapes for the regular two-operand cases: pop, compute, push,
// advance. Special cases (division, shifts, conversions) stay inline.
void i32Binop(Execution& ex, u32 (*fn)(u32, u32)) {
    const u32 b = static_cast<u32>(ex.popI32());
    const u32 a = static_cast<u32>(ex.popI32());
    ex.push(Value(static_cast<i32>(fn(a, b))));
    ++ex.frame().ip;
}

void i32Compare(Execution& ex, i32 (*fn)(i32, i32)) {
    const i32 b = ex.popI32();
    const i32 a = ex.popI32();
    ex.push(Value(fn(a, b)));
    ++ex.frame().ip;
}

void f32Binop(Execution& ex, f32 (*fn)(f32, f32)) {
    const f32 b = ex.popF32();
    const f32 a = ex.popF32();
    ex.push(Value(fn(a, b)));
    ++ex.frame().ip;
}

void f32Compare(Execution& ex, i32 (*fn)(f32, f32)) {
    const f32 b = ex.popF32();
    const f32 a = ex.popF32();
    ex.push(Value(fn(a, b)));
    ++ex.frame().ip;
}

} // namespace

// Shared with the curve rasterizer so distance queries and strokes use the
// same bounded, pinned search.
f32 distBezier(const Vec2& p, const Vec2& a, const Vec2& b, const Vec2& c) {
    return quadraticBezierDistance(p, a, b, c);
}

f32 distBezier(const Vec2& p, const Vec2& a, const Vec2& b, const Vec2& c, const Vec2& d) {
    return cubicBezierDistance(p, a, b, c, d);
}

// Integer, float and colour arithmetic (spec §9.3-§9.5, determinism §13).
bool stepArithmetic(Execution& ex, Instruction ins) {
    switch (ins.opcode) {
    // --- Integer arithmetic (wraps in u32, spec §13.1) ---
    case Opcode::I32Add:
        i32Binop(ex, [](u32 a, u32 b) { return a + b; });
        return true;
    case Opcode::I32Sub:
        i32Binop(ex, [](u32 a, u32 b) { return a - b; });
        return true;
    case Opcode::I32Mul:
        i32Binop(ex, [](u32 a, u32 b) { return a * b; });
        return true;
    case Opcode::I32DivS: {
        const i32 b = ex.popI32();
        const i32 a = ex.popI32();
        if (b == 0) ex.trap("divide by zero");
        if (a == std::numeric_limits<i32>::min() && b == -1) {
            ex.trap("integer overflow");
        }
        ex.push(Value(a / b));
        ++ex.frame().ip;
        return true;
    }
    case Opcode::I32DivU: {
        const u32 b = static_cast<u32>(ex.popI32());
        const u32 a = static_cast<u32>(ex.popI32());
        if (b == 0) ex.trap("divide by zero");
        ex.push(Value(static_cast<i32>(a / b)));
        ++ex.frame().ip;
        return true;
    }
    case Opcode::I32RemS: {
        const i32 b = ex.popI32();
        const i32 a = ex.popI32();
        if (b == 0) ex.trap("divide by zero");
        if (a == std::numeric_limits<i32>::min() && b == -1) {
            ex.push(Value(i32{0}));
        } else {
            ex.push(Value(a % b));
        }
        ++ex.frame().ip;
        return true;
    }
    case Opcode::I32RemU: {
        const u32 b = static_cast<u32>(ex.popI32());
        const u32 a = static_cast<u32>(ex.popI32());
        if (b == 0) ex.trap("divide by zero");
        ex.push(Value(static_cast<i32>(a % b)));
        ++ex.frame().ip;
        return true;
    }
    case Opcode::I32And: {
        const i32 b = ex.popI32();
        const i32 a = ex.popI32();
        ex.push(Value(static_cast<i32>(static_cast<u32>(a) & static_cast<u32>(b))));
        ++ex.frame().ip;
        return true;
    }
    case Opcode::I32Or: {
        const i32 b = ex.popI32();
        const i32 a = ex.popI32();
        ex.push(Value(static_cast<i32>(static_cast<u32>(a) | static_cast<u32>(b))));
        ++ex.frame().ip;
        return true;
    }
    case Opcode::I32Xor: {
        const i32 b = ex.popI32();
        const i32 a = ex.popI32();
        ex.push(Value(static_cast<i32>(static_cast<u32>(a) ^ static_cast<u32>(b))));
        ++ex.frame().ip;
        return true;
    }
    case Opcode::I32Shl: {
        const u32 shift = static_cast<u32>(ex.popI32()) & 31u;
        const i32 a = ex.popI32();
        ex.push(Value(static_cast<i32>(static_cast<u32>(a) << shift)));
        ++ex.frame().ip;
        return true;
    }
    case Opcode::I32ShrS: {
        const u32 shift = static_cast<u32>(ex.popI32()) & 31u;
        const i32 a = ex.popI32();
        ex.push(Value(shiftRightSigned(a, shift)));
        ++ex.frame().ip;
        return true;
    }
    case Opcode::I32ShrU: {
        const u32 shift = static_cast<u32>(ex.popI32()) & 31u;
        const i32 a = ex.popI32();
        ex.push(Value(static_cast<i32>(static_cast<u32>(a) >> shift)));
        ++ex.frame().ip;
        return true;
    }
    case Opcode::I32Neg: {
        const i32 a = ex.popI32();
        ex.push(Value(static_cast<i32>(0u - static_cast<u32>(a))));
        ++ex.frame().ip;
        return true;
    }
    case Opcode::I32Not: {
        const i32 a = ex.popI32();
        ex.push(Value(static_cast<i32>(~static_cast<u32>(a))));
        ++ex.frame().ip;
        return true;
    }

    // --- Integer comparisons (spec §9.3) ---
    case Opcode::I32Eq:
        i32Compare(ex, [](i32 a, i32 b) { return a == b ? 1 : 0; });
        return true;
    case Opcode::I32Ne:
        i32Compare(ex, [](i32 a, i32 b) { return a != b ? 1 : 0; });
        return true;
    case Opcode::I32LtS:
        i32Compare(ex, [](i32 a, i32 b) { return a < b ? 1 : 0; });
        return true;
    case Opcode::I32LtU:
        i32Compare(ex, [](i32 a, i32 b) {
            return static_cast<u32>(a) < static_cast<u32>(b) ? 1 : 0;
        });
        return true;
    case Opcode::I32GtS:
        i32Compare(ex, [](i32 a, i32 b) { return a > b ? 1 : 0; });
        return true;
    case Opcode::I32GtU:
        i32Compare(ex, [](i32 a, i32 b) {
            return static_cast<u32>(a) > static_cast<u32>(b) ? 1 : 0;
        });
        return true;
    case Opcode::I32LeS:
        i32Compare(ex, [](i32 a, i32 b) { return a <= b ? 1 : 0; });
        return true;
    case Opcode::I32LeU:
        i32Compare(ex, [](i32 a, i32 b) {
            return static_cast<u32>(a) <= static_cast<u32>(b) ? 1 : 0;
        });
        return true;
    case Opcode::I32GeS:
        i32Compare(ex, [](i32 a, i32 b) { return a >= b ? 1 : 0; });
        return true;
    case Opcode::I32GeU:
        i32Compare(ex, [](i32 a, i32 b) {
            return static_cast<u32>(a) >= static_cast<u32>(b) ? 1 : 0;
        });
        return true;

    // --- Float arithmetic (spec §9.4; no FMA contraction, spec §13.2) ---
    case Opcode::F32Add:
        f32Binop(ex, [](f32 a, f32 b) { return a + b; });
        return true;
    case Opcode::F32Sub:
        f32Binop(ex, [](f32 a, f32 b) { return a - b; });
        return true;
    case Opcode::F32Mul:
        f32Binop(ex, [](f32 a, f32 b) { return a * b; });
        return true;
    case Opcode::F32Div:
        f32Binop(ex, [](f32 a, f32 b) { return a / b; }); // IEEE: x/0 = inf
        return true;
    case Opcode::F32Eq:
        f32Compare(ex, [](f32 a, f32 b) { return a == b ? 1 : 0; }); // NaN: false
        return true;
    case Opcode::F32Ne:
        f32Compare(ex, [](f32 a, f32 b) { return a != b ? 1 : 0; }); // NaN: true
        return true;
    case Opcode::F32Lt:
        f32Compare(ex, [](f32 a, f32 b) { return a < b ? 1 : 0; });
        return true;
    case Opcode::F32Gt:
        f32Compare(ex, [](f32 a, f32 b) { return a > b ? 1 : 0; });
        return true;
    case Opcode::F32Le:
        f32Compare(ex, [](f32 a, f32 b) { return a <= b ? 1 : 0; });
        return true;
    case Opcode::F32Ge:
        f32Compare(ex, [](f32 a, f32 b) { return a >= b ? 1 : 0; });
        return true;
    case Opcode::F32Neg: {
        ex.push(Value(-ex.popF32()));
        ++ex.frame().ip;
        return true;
    }
    case Opcode::F32Abs: {
        ex.push(Value(std::fabs(ex.popF32())));
        ++ex.frame().ip;
        return true;
    }
    case Opcode::F32Sqrt: {
        ex.push(Value(std::sqrt(ex.popF32())));
        ++ex.frame().ip;
        return true;
    }
    case Opcode::F32Sin: {
        ex.push(Value(f32Sin(ex.popF32())));
        ++ex.frame().ip;
        return true;
    }
    case Opcode::F32Cos: {
        ex.push(Value(f32Cos(ex.popF32())));
        ++ex.frame().ip;
        return true;
    }
    case Opcode::F32Floor: {
        ex.push(Value(static_cast<f32>(std::floor(ex.popF32()))));
        ++ex.frame().ip;
        return true;
    }
    case Opcode::F32Ceil: {
        ex.push(Value(static_cast<f32>(std::ceil(ex.popF32()))));
        ++ex.frame().ip;
        return true;
    }
    case Opcode::F32Min: {
        const f32 b = ex.popF32();
        const f32 a = ex.popF32();
        ex.push(Value(f32MinMax(a, b, false)));
        ++ex.frame().ip;
        return true;
    }
    case Opcode::F32Max: {
        const f32 b = ex.popF32();
        const f32 a = ex.popF32();
        ex.push(Value(f32MinMax(a, b, true)));
        ++ex.frame().ip;
        return true;
    }
    case Opcode::F32Pow: {
        const f32 b = ex.popF32();
        const f32 a = ex.popF32();
        ex.push(Value(f32Pow(a, b)));
        ++ex.frame().ip;
        return true;
    }
    case Opcode::F32Exp: {
        ex.push(Value(f32Exp(ex.popF32())));
        ++ex.frame().ip;
        return true;
    }
    case Opcode::F32Atan2: {
        const f32 b = ex.popF32(); // x
        const f32 a = ex.popF32(); // y
        ex.push(Value(f32Atan2(a, b)));
        ++ex.frame().ip;
        return true;
    }
    case Opcode::Vec2Make: {
        const f32 y = ex.popF32();
        const f32 x = ex.popF32();
        ex.push(Value(Vec2{x, y}));
        ++ex.frame().ip;
        return true;
    }
    case Opcode::Vec2Add: {
        const Vec2 b = ex.popVec2();
        const Vec2 a = ex.popVec2();
        ex.push(Value(Vec2{a.x + b.x, a.y + b.y}));
        ++ex.frame().ip;
        return true;
    }
    case Opcode::Vec2Sub: {
        const Vec2 b = ex.popVec2();
        const Vec2 a = ex.popVec2();
        ex.push(Value(Vec2{a.x - b.x, a.y - b.y}));
        ++ex.frame().ip;
        return true;
    }
    case Opcode::Vec2Mul: {
        const Value right = ex.pop();
        const Vec2 a = ex.popVec2();
        if (const Vec2* b = std::get_if<Vec2>(&right)) {
            ex.push(Value(Vec2{a.x * b->x, a.y * b->y}));
        } else {
            const f32 s = std::get<f32>(right);
            ex.push(Value(Vec2{a.x * s, a.y * s}));
        }
        ++ex.frame().ip;
        return true;
    }
    case Opcode::Vec2Div: {
        const Value right = ex.pop();
        const Vec2 a = ex.popVec2();
        if (const Vec2* b = std::get_if<Vec2>(&right)) {
            ex.push(Value(Vec2{a.x / b->x, a.y / b->y}));
        } else {
            const f32 s = std::get<f32>(right);
            ex.push(Value(Vec2{a.x / s, a.y / s}));
        }
        ++ex.frame().ip;
        return true;
    }
    case Opcode::Vec2Neg: {
        const Vec2 v = ex.popVec2();
        ex.push(Value(Vec2{-v.x, -v.y}));
        ++ex.frame().ip;
        return true;
    }
    case Opcode::Vec2Len: {
        const Vec2 v = ex.popVec2();
        const f32 xx = v.x * v.x;
        const f32 yy = v.y * v.y;
        ex.push(Value(std::sqrt(xx + yy)));
        ++ex.frame().ip;
        return true;
    }
    case Opcode::Vec2Dot: {
        const Vec2 b = ex.popVec2();
        const Vec2 a = ex.popVec2();
        const f32 abx = a.x * b.x;
        const f32 aby = a.y * b.y;
        ex.push(Value(abx + aby));
        ++ex.frame().ip;
        return true;
    }
    case Opcode::Vec2Rot: {
        const f32 angle = ex.popF32();
        const Vec2 v = ex.popVec2();
        const f32 c = f32Cos(angle);
        const f32 s = f32Sin(angle);
        const f32 xc = v.x * c;
        const f32 ys = v.y * s;
        const f32 xs = v.x * s;
        const f32 yc = v.y * c;
        ex.push(Value(Vec2{xc - ys, xs + yc}));
        ++ex.frame().ip;
        return true;
    }
    case Opcode::Vec2X: {
        ex.push(Value(ex.popVec2().x));
        ++ex.frame().ip;
        return true;
    }
    case Opcode::Vec2Y: {
        ex.push(Value(ex.popVec2().y));
        ++ex.frame().ip;
        return true;
    }
    case Opcode::DistCircle: {
        const f32 r = ex.popF32();
        const Vec2 c = ex.popVec2();
        const Vec2 p = ex.popVec2();
        ex.push(Value(distCircle(p, c, r)));
        ++ex.frame().ip;
        return true;
    }
    case Opcode::DistRect: {
        const Vec2 half = ex.popVec2();
        const Vec2 c = ex.popVec2();
        const Vec2 p = ex.popVec2();
        ex.push(Value(distRect(p, c, half)));
        ++ex.frame().ip;
        return true;
    }
    case Opcode::DistRoundRect: {
        const f32 radius = ex.popF32();
        const Vec2 half = ex.popVec2();
        const Vec2 c = ex.popVec2();
        const Vec2 p = ex.popVec2();
        ex.push(Value(distRoundRect(p, c, half, radius)));
        ++ex.frame().ip;
        return true;
    }
    case Opcode::DistLine: {
        const Vec2 b = ex.popVec2();
        const Vec2 a = ex.popVec2();
        const Vec2 p = ex.popVec2();
        ex.push(Value(distLine(p, a, b)));
        ++ex.frame().ip;
        return true;
    }
    case Opcode::DistBezier: {
        const Vec2 c = ex.popVec2();
        const Vec2 b = ex.popVec2();
        const Vec2 a = ex.popVec2();
        const Vec2 p = ex.popVec2();
        ex.push(Value(distBezier(p, a, b, c)));
        ++ex.frame().ip;
        return true;
    }
    case Opcode::DistBezierCubic: {
        const Vec2 d = ex.popVec2();
        const Vec2 c = ex.popVec2();
        const Vec2 b = ex.popVec2();
        const Vec2 a = ex.popVec2();
        const Vec2 p = ex.popVec2();
        ex.push(Value(distBezier(p, a, b, c, d)));
        ++ex.frame().ip;
        return true;
    }
    case Opcode::Fill: {
        const f32 w = ex.popF32();
        const f32 d = ex.popF32();
        ex.push(Value(sdfFill(d, w)));
        ++ex.frame().ip;
        return true;
    }
    case Opcode::Gain: {
        const f32 k = ex.popF32();
        const f32 t = ex.popF32();
        ex.push(Value(gainCurve(t, k)));
        ++ex.frame().ip;
        return true;
    }
    case Opcode::Quintic: {
        ex.push(Value(quinticCurve(ex.popF32())));
        ++ex.frame().ip;
        return true;
    }
    case Opcode::I32ToF32: {
        ex.push(Value(static_cast<f32>(ex.popI32())));
        ++ex.frame().ip;
        return true;
    }
    case Opcode::F32ToI32: {
        ex.push(Value(f32ToI32(ex.popF32())));
        ++ex.frame().ip;
        return true;
    }

    // --- Colour (spec §9.5/§12.6) ---
    case Opcode::ColorMix: {
        const f32 t = std::clamp(ex.popF32(), 0.f, 1.f);
        const Color b = ex.popColor();
        const Color a = ex.popColor();
        // Explicit fused multiply-add: the QuantSpace reference builds
        // contract this expression, so the pinned formula reproduces its
        // exact output bit for bit (spec §12.6). std::fma is deterministic.
        const auto channel = [t](u8 x, u8 y) {
            return static_cast<u8>(std::fma(static_cast<f32>(x), 1.f - t,
                                            static_cast<f32>(y) * t));
        };
        ex.push(Value(Color{channel(a.r, b.r), channel(a.g, b.g), channel(a.b, b.b), a.a}));
        ++ex.frame().ip;
        return true;
    }
    case Opcode::ColorShade: {
        const i32 amount = ex.popI32();
        const Color c = ex.popColor();
        const auto channel = [amount](u8 x) {
            return static_cast<u8>(std::clamp(static_cast<int>(x) + amount, 0, 255));
        };
        ex.push(Value(Color{channel(c.r), channel(c.g), channel(c.b), c.a}));
        ++ex.frame().ip;
        return true;
    }
    case Opcode::ColorSetA: {
        const i32 amount = ex.popI32();
        const Color c = ex.popColor();
        ex.push(Value(Color{c.r, c.g, c.b,
                            static_cast<u8>(std::clamp(amount, 0, 255))}));
        ++ex.frame().ip;
        return true;
    }
    case Opcode::ColorMul: {
        const Color b = ex.popColor();
        const Color a = ex.popColor();
        ex.push(Value(Color{static_cast<u8>((static_cast<u32>(a.r) * b.r) / 255u),
                            static_cast<u8>((static_cast<u32>(a.g) * b.g) / 255u),
                            static_cast<u8>((static_cast<u32>(a.b) * b.b) / 255u),
                            a.a}));
        ++ex.frame().ip;
        return true;
    }
    case Opcode::ColorScale: {
        const f32 s = ex.popF32();
        const Color c = ex.popColor();
        const auto channel = [s](u8 ch) {
            const i32 truncated = f32ToI32(static_cast<f32>(ch) * s);
            return static_cast<u8>(std::clamp(truncated, 0, 255));
        };
        ex.push(Value(Color{channel(c.r), channel(c.g), channel(c.b), c.a}));
        ++ex.frame().ip;
        return true;
    }
    case Opcode::ColorHsv: {
        const f32 valScale = ex.popF32();
        const f32 satScale = ex.popF32();
        const f32 hueShift = ex.popF32();
        const Color c = ex.popColor();
        if (!std::isfinite(hueShift)) {
            ex.trap("color.hsv hue shift must be finite");
        }
        ex.push(Value(colorHsv(c, hueShift, satScale, valScale)));
        ++ex.frame().ip;
        return true;
    }
    case Opcode::Dither: {
        const i32 y = ex.popI32();
        const i32 x = ex.popI32();
        const f32 t = ex.popF32();
        ex.push(Value(ditherThreshold(t, x, y)));
        ++ex.frame().ip;
        return true;
    }
    case Opcode::Vec2Fold: {
        const i32 n = ex.popI32();
        const Vec2 v = ex.popVec2();
        ex.push(Value(vec2Fold(v, n)));
        ++ex.frame().ip;
        return true;
    }
    case Opcode::Vec2Polar: {
        ex.push(Value(vec2Polar(ex.popVec2())));
        ++ex.frame().ip;
        return true;
    }

    default:
        return false;
    }
}

} // namespace iprop::detail
