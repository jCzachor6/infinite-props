#include "iprop/math/Bezier.hpp"
#include "iprop/math/Sdf.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace iprop::math {
namespace {

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

} // namespace

// Shared with the curve rasterizer so distance queries and strokes use the
// same bounded, pinned search.
f32 distBezier(const Vec2& p, const Vec2& a, const Vec2& b, const Vec2& c) {
    return quadraticBezierDistance(p, a, b, c);
}

f32 distBezier(const Vec2& p, const Vec2& a, const Vec2& b, const Vec2& c, const Vec2& d) {
    return cubicBezierDistance(p, a, b, c, d);
}

} // namespace iprop::math
