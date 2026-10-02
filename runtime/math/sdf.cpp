#include "iprop/math/Sdf.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace iprop::math {

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

f32 sdfFill(f32 d, f32 w) {
    const f32 t0 = d / w;
    const f32 t1 = t0 + 0.5f;
    const f32 t = std::fmin(std::fmax(t1, 0.f), 1.f);
    const f32 tt = t * t;
    const f32 s = 3.f - 2.f * t;
    return 1.f - tt * s;
}

} // namespace iprop::math
