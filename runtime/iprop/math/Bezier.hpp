#pragma once
#include "iprop/Types.hpp"
#include <array>
#include <cstddef>
#include <span>

namespace iprop::math {

// A value kernel prepared once for many distance queries. Factories cache only
// control-dependent expressions; Newton iteration and rounding remain pinned.
class BezierKernel {
public:
    static BezierKernel quadratic(Vec2 a, Vec2 b, Vec2 c);
    static BezierKernel cubic(Vec2 a, Vec2 b, Vec2 c, Vec2 d);
    f32 distanceTo(Vec2 point) const;
    // Exactly `distanceTo(point) <= radius`, for strokes: the search stops at
    // the first evaluated point that is certainly inside.
    bool within(Vec2 point, f32 radius) const;

    // Boxes that together contain every point within `radius` of anything
    // the search can evaluate, with a wide margin for rounding. A point
    // outside all of them is never `within(point, radius)`. Returns how many
    // boxes were written, or 0 when no bound is known and nothing may be
    // skipped.
    struct Box {
        double left = 0, top = 0, right = 0, bottom = 0;
    };
    std::size_t cover(std::span<Box> boxes, f32 radius) const;

private:
    BezierKernel() = default;
    static BezierKernel create(std::array<Vec2,4> points, bool cubic);
    f32 nearestQuadratic(const Vec2& p, f32 enough) const;
    f32 nearestCubic(const Vec2& p, f32 enough) const;
    std::array<Vec2,4> points_{};
    std::array<Vec2,3> differences_{};
    std::array<Vec2,2> second_{};
    bool cubic_ = false;
    bool finite_ = false;
    bool point_ = false;
};

f32 distBezier(const Vec2& p, const Vec2& a, const Vec2& b, const Vec2& c);
f32 distBezier(const Vec2& p, const Vec2& a, const Vec2& b, const Vec2& c, const Vec2& d);

} // namespace iprop::math
