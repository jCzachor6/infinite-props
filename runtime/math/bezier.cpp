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

// The smallest squared distance from p to the curve's ends and to every
// evaluated point of the pinned search: five candidates of at most five
// Newton evaluations each. The search itself never reads a distance, and a
// square root is monotone and correctly rounded, so the nearest point's root
// is the smallest of all the roots; callers take one root at the end.
// A squared distance at or below `enough` ends the search early; pass a
// negative value to always complete it.
template <typename Point, typename Derivative, typename SecondDerivative>
f32 nearestSquared(const Vec2& p, const Vec2& startPoint, const Vec2& endPoint, f32 enough,
                   Point point, Derivative derivative, SecondDerivative secondDerivative) {
    const auto squared = [&](const Vec2& q) {
        const f32 dx = p.x - q.x;
        const f32 dy = p.y - q.y;
        return dx * dx + dy * dy;
    };
    f32 best = std::min(squared(startPoint), squared(endPoint));
    if (best <= enough) return best;
    const f32 candidates[5] = {0.f, 0.25f, 0.5f, 0.75f, 1.f};
    for (const f32 start : candidates) {
        f32 t = start;
        for (int step = 0; step <= 4; ++step) {
            const Vec2 pt = point(t);
            const f32 rx = pt.x - p.x;
            const f32 ry = pt.y - p.y;
            best = std::min(best, rx * rx + ry * ry);
            if (best <= enough) return best;
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
    return std::sqrt(cubic_ ? nearestCubic(p, -1.f) : nearestQuadratic(p, -1.f));
}

bool BezierKernel::within(Vec2 p, f32 radius) const {
    if (!finite_ || !finitePoint(p)) return false; // a NaN distance compares false
    if (point_) return distCircle(p,points_[0],0.f) <= radius;
    // One float below the rounded square is at most the exact square, and a
    // squared distance at most radius * radius has a root at most radius.
    const f32 enough = radius >= 0.f ? std::nextafter(radius * radius, 0.f) : -1.f;
    const f32 nearest = cubic_ ? nearestCubic(p, enough) : nearestQuadratic(p, enough);
    return nearest <= enough || std::sqrt(nearest) <= radius;
}

// The curve is cut into equal parameter steps. Between two samples a
// coordinate leaves their chord by at most step^2 / 8 times the largest
// second derivative, so the samples' box grown by that sag holds the piece.
// Everything here is binary64; the search works in binary32 and its points
// and distances are off by a few units in the last place of the largest
// coordinate, which the relative margins below exceed a hundredfold.
std::size_t BezierKernel::cover(std::span<Box> boxes, f32 radius) const {
    if (!finite_ || point_ || boxes.empty() || !(radius >= 0.f) || !(radius <= 1e30f)) return 0;
    const std::size_t count = cubic_ ? 4 : 3;
    double extent = 0.0;
    for (std::size_t i = 0; i < count; ++i)
        extent = std::max({extent, std::fabs(static_cast<double>(points_[i].x)), std::fabs(static_cast<double>(points_[i].y))});
    if (!(extent <= 1e30)) return 0;

    const auto coordinate = [&](std::size_t i, bool vertical) {
        return static_cast<double>(vertical ? points_[i].y : points_[i].x);
    };
    const auto at = [&](double t, bool vertical) {
        const double s = 1.0 - t;
        if (!cubic_)
            return s * s * coordinate(0, vertical) + 2.0 * s * t * coordinate(1, vertical) + t * t * coordinate(2, vertical);
        return s * s * s * coordinate(0, vertical) + 3.0 * s * s * t * coordinate(1, vertical) +
            3.0 * s * t * t * coordinate(2, vertical) + t * t * t * coordinate(3, vertical);
    };
    const auto bend = [&](bool vertical) {
        const auto second = [&](std::size_t i) {
            return std::fabs(coordinate(i, vertical) - 2.0 * coordinate(i + 1, vertical) + coordinate(i + 2, vertical));
        };
        return cubic_ ? 6.0 * std::max(second(0), second(1)) : 2.0 * second(0);
    };
    const double step = 1.0 / static_cast<double>(boxes.size());
    const double slack = static_cast<double>(radius) * 1.00001 + extent * 0.00001;
    const double growX = step * step / 8.0 * bend(false) * 1.00001 + slack;
    const double growY = step * step / 8.0 * bend(true) * 1.00001 + slack;
    double x = at(0.0, false), y = at(0.0, true);
    for (std::size_t i = 0; i < boxes.size(); ++i) {
        const double t = i + 1 == boxes.size() ? 1.0 : static_cast<double>(i + 1) * step;
        const double nextX = at(t, false), nextY = at(t, true);
        boxes[i] = {std::min(x, nextX) - growX, std::min(y, nextY) - growY,
                    std::max(x, nextX) + growX, std::max(y, nextY) + growY};
        x = nextX;
        y = nextY;
    }
    return boxes.size();
}

f32 BezierKernel::nearestQuadratic(const Vec2& p, f32 enough) const {
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
    return nearestSquared(p, a, c, enough, point, derivative, [&](f32) { return dd; });
}

f32 BezierKernel::nearestCubic(const Vec2& p, f32 enough) const {
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
    return nearestSquared(p, a, d, enough, point, derivative, secondDerivative);
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
