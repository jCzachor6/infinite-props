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

} // namespace

BezierKernel BezierKernel::create(std::array<Vec2,4> points, bool cubic) {
    BezierKernel kernel;
    kernel.points_ = points;
    kernel.cubic_ = cubic;
    kernel.finite_ = std::all_of(points.begin(), points.end(), finitePoint);
    kernel.point_ = std::all_of(points.begin(), points.end(), [&](Vec2 p) { return p == points[0]; });
    if (!kernel.finite_ || kernel.point_) return kernel;
    for (std::size_t i = 0; i < 3; ++i) {
        kernel.differences_[i] = {points[i+1].x-points[i].x, points[i+1].y-points[i].y};
    }
    for (std::size_t i = 0; i < 2; ++i) {
        kernel.second_[i] = {points[i].x-(points[i+1].x+points[i+1].x)+points[i+2].x,
                             points[i].y-(points[i+1].y+points[i+1].y)+points[i+2].y};
    }
    if (!cubic) {
        const auto dd = kernel.second_[0];
        kernel.second_[0] = {dd.x+dd.x, dd.y+dd.y};
    }
    return kernel;
}

BezierKernel BezierKernel::quadratic(Vec2 a, Vec2 b, Vec2 c) {
    return create({a,b,c,c},false);
}

BezierKernel BezierKernel::cubic(Vec2 a, Vec2 b, Vec2 c, Vec2 d) {
    return create({a,b,c,d},true);
}

f32 BezierKernel::distanceTo(Vec2 p) const {
    if (!finite_ || !finitePoint(p)) return std::numeric_limits<f32>::quiet_NaN();
    if (point_) return distCircle(p,points_[0],0.f);
    return cubic_ ? distanceCubic(p) : distanceQuadratic(p);
}

f32 BezierKernel::distanceQuadratic(const Vec2& p) const {
    const Vec2 a = points_[0], b = points_[1], c = points_[2];
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
        return {w0 * differences_[0].x + w1 * differences_[1].x,
                w0 * differences_[0].y + w1 * differences_[1].y};
    };
    const Vec2 dd = second_[0];
    return bezierDistance(p, a, c, point, derivative, [&](f32) { return dd; });
}

f32 BezierKernel::distanceCubic(const Vec2& p) const {
    const Vec2 a = points_[0], b = points_[1], c = points_[2], d = points_[3];
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
        return {w0 * differences_[0].x + w1 * differences_[1].x + w2 * differences_[2].x,
                w0 * differences_[0].y + w1 * differences_[1].y + w2 * differences_[2].y};
    };
    const Vec2 ab = second_[0], bc = second_[1];
    const auto secondDerivative = [&](f32 t) -> Vec2 {
        const f32 w0 = 6.f * (1.f - t);
        const f32 w1 = 6.f * t;
        return {w0 * ab.x + w1 * bc.x, w0 * ab.y + w1 * bc.y};
    };
    return bezierDistance(p, a, d, point, derivative, secondDerivative);
}

// Shared with the curve rasterizer so distance queries and strokes use the
// same bounded, pinned search.
f32 distBezier(const Vec2& p, const Vec2& a, const Vec2& b, const Vec2& c) {
    return BezierKernel::quadratic(a,b,c).distanceTo(p);
}

f32 distBezier(const Vec2& p, const Vec2& a, const Vec2& b, const Vec2& c, const Vec2& d) {
    return BezierKernel::cubic(a,b,c,d).distanceTo(p);
}

} // namespace iprop::math
