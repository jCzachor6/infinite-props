#pragma once
#include "iprop/Types.hpp"
#include <array>

namespace iprop::math {

// A value kernel prepared once for many distance queries. Factories cache only
// control-dependent expressions; Newton iteration and rounding remain pinned.
class BezierKernel {
public:
    static BezierKernel quadratic(Vec2 a, Vec2 b, Vec2 c);
    static BezierKernel cubic(Vec2 a, Vec2 b, Vec2 c, Vec2 d);
    f32 distanceTo(Vec2 point) const;

private:
    BezierKernel() = default;
    static BezierKernel create(std::array<Vec2,4> points, bool cubic);
    f32 distanceQuadratic(const Vec2& p) const;
    f32 distanceCubic(const Vec2& p) const;
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
