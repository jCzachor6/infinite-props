#include "iprop/VmInternals.hpp"
#include "iprop/math/Bezier.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace iprop::detail {
namespace {

// Scaled, rounded coordinate (spec §8.1: multiply by the scale factor, then
// round to the nearest integer, half away from zero). Non-finite or
// out-of-range values trap: a cast out of i32 range would be UB.
i32 scaledCoord(Execution& ex, f32 value, f32 scale) {
    const f32 scaled = value * scale;
    if (!std::isfinite(scaled) || scaled < -2147483648.f || scaled >= 2147483648.f) {
        ex.trap("coordinate out of range");
    }
    return static_cast<i32>(std::round(scaled));
}

// A raster coordinate: floor/ceil must stay within i32 (spec §11 formulas).
i32 coordToInt(Execution& ex, f32 value, const char* what) {
    if (!std::isfinite(value) || value < -2147483648.f || value >= 2147483648.f) {
        ex.trap(std::string("coordinate out of range (") + what + ")");
    }
    return static_cast<i32>(value);
}

// Saturating visit-count product: a huge bounding box costs the maximum
// fuel, which then traps, instead of overflowing u64.
u64 visitCount(i64 dx, i64 dy) {
    if (dx <= 0 || dy <= 0) {
        return 0;
    }
    if (static_cast<u64>(dx) > UINT64_MAX / static_cast<u64>(dy)) {
        return UINT64_MAX;
    }
    return static_cast<u64>(dx) * static_cast<u64>(dy);
}

// A round-capped stroke sampled at integer raster coordinates, like oval.
// Charge the full expanded control box even when it lies off canvas.
void strokeCurve(Execution& ex, std::array<Vec2, 4> points, std::size_t count,
                  f32 thickness, const Color& colour) {
    if (!std::isfinite(thickness)) {
        ex.trap("curve thickness out of range");
    }
    if (thickness <= 0.f) {
        return;
    }
    for (std::size_t i = 0; i < count; ++i) {
        points[i].x *= ex.canvas.scaleX;
        points[i].y *= ex.canvas.scaleY;
        if (!std::isfinite(points[i].x) || !std::isfinite(points[i].y)) {
            ex.trap("coordinate out of range (curve control)");
        }
    }
    f32 left = points[0].x, right = left, top = points[0].y, bottom = top;
    for (std::size_t i = 1; i < count; ++i) {
        left = std::min(left, points[i].x);
        right = std::max(right, points[i].x);
        top = std::min(top, points[i].y);
        bottom = std::max(bottom, points[i].y);
    }
    const f32 radius = thickness * 0.5f;
    const i32 x0 = coordToInt(ex, std::floor(left - radius), "curve x");
    const i32 x1 = coordToInt(ex, std::ceil(right + radius), "curve x");
    const i32 y0 = coordToInt(ex, std::floor(top - radius), "curve y");
    const i32 y1 = coordToInt(ex, std::ceil(bottom + radius), "curve y");
    ex.spend(visitCount(static_cast<i64>(x1) - x0 + 1, static_cast<i64>(y1) - y0 + 1));
    const i32 clipX1 = std::min(x1, static_cast<i32>(ex.canvas.width) - 1);
    const i32 clipY1 = std::min(y1, static_cast<i32>(ex.canvas.height) - 1);
    for (i32 y = std::max(y0, 0); y <= clipY1; ++y) {
        for (i32 x = std::max(x0, 0); x <= clipX1; ++x) {
            const Vec2 p{static_cast<f32>(x), static_cast<f32>(y)};
            const f32 distance = count == 3
                ? math::distBezier(p, points[0], points[1], points[2])
                : math::distBezier(p, points[0], points[1], points[2], points[3]);
            if (distance <= radius) {
                compositePixel(ex, x, y, colour);
            }
        }
    }
}

// §11.2 point-in-polygon fill over scaled (f32) coordinates. Charges the
// bounding-box visit count up front (spec §8.3/§11.2).
void fillPolygon(Execution& ex, const std::vector<Vec2>& points, const Color& colour) {
    if (points.empty()) {
        return;
    }
    f32 left = points[0].x, right = points[0].x, top = points[0].y, bottom = points[0].y;
    for (const auto& p : points) {
        left = std::min(left, p.x);
        right = std::max(right, p.x);
        top = std::min(top, p.y);
        bottom = std::max(bottom, p.y);
    }
    const i32 x0 = coordToInt(ex, std::floor(left), "poly x");
    const i32 x1 = coordToInt(ex, std::ceil(right), "poly x");
    const i32 y0 = coordToInt(ex, std::floor(top), "poly y");
    const i32 y1 = coordToInt(ex, std::ceil(bottom), "poly y");
    ex.spend(visitCount(static_cast<i64>(x1) - x0 + 1, static_cast<i64>(y1) - y0 + 1));

    const std::size_t n = points.size();
    for (i32 py = y0; py <= y1; ++py) {
        for (i32 px = x0; px <= x1; ++px) {
            bool inside = false;
            const f32 cx = static_cast<f32>(px) + .5f;
            const f32 cy = static_cast<f32>(py) + .5f;
            for (std::size_t i = 0; i < n; ++i) {
                const std::size_t j = (i + n - 1) % n;
                const auto& a = points[i]; // current vertex
                const auto& d = points[j]; // previous vertex
                // Pixel centres and the QuantSpace operation order, for
                // bit-compatible edge pixels (spec §11.2).
                if ((a.y > cy) != (d.y > cy) &&
                    cx < (d.x - a.x) * (cy - a.y) / (d.y - a.y) + a.x) {
                    inside = !inside;
                }
            }
            if (inside) {
                compositePixel(ex, px, py, colour);
            }
        }
    }
}

} // namespace

// §11.1 compositing, shared with the part blit (declared in VmInternals.hpp).
void compositePixel(Execution& ex, i32 x, i32 y, const Color& src) {
    Canvas& canvas = ex.canvas;
    if (!canvas.contains(x, y)) {
        return;
    }
    Color& dst = canvas.at(static_cast<u32>(x), static_cast<u32>(y));
    if (src.a == 255) {
        dst = src;
        return;
    }
    const u32 a = src.a;
    const u32 b = 255u - a;
    dst = {static_cast<u8>((static_cast<u32>(src.r) * a + static_cast<u32>(dst.r) * b) / 255u),
           static_cast<u8>((static_cast<u32>(src.g) * a + static_cast<u32>(dst.g) * b) / 255u),
           static_cast<u8>((static_cast<u32>(src.b) * a + static_cast<u32>(dst.b) * b) / 255u),
           dst.a}; // destination alpha preserved
}

// Canvas operations (spec §9.8, exact rasterization §11).
bool stepCanvas(Execution& ex, Instruction ins) {
    const f32 sX = ex.canvas.scaleX;
    const f32 sY = ex.canvas.scaleY;
    switch (ins.opcode) {
    case Opcode::Pixel: {
        const Color colour = ex.popColor();
        const i32 y = scaledCoord(ex, static_cast<f32>(ex.popI32()), sY);
        const i32 x = scaledCoord(ex, static_cast<f32>(ex.popI32()), sX);
        compositePixel(ex, x, y, colour);
        ++ex.frame().ip;
        return true;
    }

    case Opcode::Rect: {
        const Color colour = ex.popColor();
        const f32 h = ex.popF32() * sY;
        const f32 w = ex.popF32() * sX;
        const f32 y = ex.popF32() * sY;
        const f32 x = ex.popF32() * sX;
        fillPolygon(ex, {{x, y}, {x + w, y}, {x + w, y + h}, {x, y + h}}, colour);
        ++ex.frame().ip;
        return true;
    }

    case Opcode::Line: {
        const Color colour = ex.popColor();
        const i32 thickness = ex.popI32();
        const f32 by = ex.popF32() * sY;
        const f32 bx = ex.popF32() * sX;
        const f32 ay = ex.popF32() * sY;
        const f32 ax = ex.popF32() * sX;
        const f32 dx = bx - ax;
        const f32 dy = by - ay;
        const i32 steps = std::max(
            1, coordToInt(ex, std::ceil(std::max(std::fabs(dx), std::fabs(dy))), "line length"));
        if (thickness > 0) {
            ex.spend(visitCount(static_cast<i64>(steps) + 1, thickness));
            for (i32 i = 0; i <= steps; ++i) {
                const f32 t = static_cast<f32>(i) / static_cast<f32>(steps);
                const i32 ix = static_cast<i32>(std::round(ax + dx * t));
                const i32 iy = static_cast<i32>(std::round(ay + dy * t));
                for (i32 q = 0; q < thickness; ++q) {
                    compositePixel(ex, ix, iy + q, colour);
                }
            }
        }
        ++ex.frame().ip;
        return true;
    }

    case Opcode::Curve:
    case Opcode::CurveCubic: {
        const Color colour = ex.popColor();
        const f32 thickness = ex.popF32();
        const std::size_t count = ins.opcode == Opcode::Curve ? 3 : 4;
        std::array<Vec2, 4> points{};
        for (std::size_t i = count; i > 0; --i) {
            points[i - 1] = ex.popVec2();
        }
        strokeCurve(ex, points, count, thickness, colour);
        ++ex.frame().ip;
        return true;
    }

    case Opcode::Poly: {
        const Color colour = ex.popColor();
        const u16 count = ins.operand;
        std::vector<Vec2> points(count);
        for (u16 i = count; i > 0; --i) {
            const f32 y = ex.popF32() * sY;
            const f32 x = ex.popF32() * sX;
            points[i - 1] = {x, y};
        }
        fillPolygon(ex, points, colour);
        ++ex.frame().ip;
        return true;
    }

    case Opcode::Oval: {
        const Color colour = ex.popColor();
        const f32 ry = ex.popF32() * sY;
        const f32 rx = ex.popF32() * sX;
        const f32 y = ex.popF32() * sY;
        const f32 x = ex.popF32() * sX;
        const i32 y0 = coordToInt(ex, std::floor(y - ry), "oval y");
        const i32 y1 = coordToInt(ex, std::ceil(y + ry), "oval y");
        const i32 x0 = coordToInt(ex, std::floor(x - rx), "oval x");
        const i32 x1 = coordToInt(ex, std::ceil(x + rx), "oval x");
        ex.spend(visitCount(static_cast<i64>(x1) - x0 + 1, static_cast<i64>(y1) - y0 + 1));
        for (i32 py = y0; py <= y1; ++py) {
            for (i32 px = x0; px <= x1; ++px) {
                const f32 nx = (static_cast<f32>(px) - x) / rx;
                const f32 ny = (static_cast<f32>(py) - y) / ry;
                if (nx * nx + ny * ny <= 1.f) {
                    compositePixel(ex, px, py, colour);
                }
            }
        }
        ++ex.frame().ip;
        return true;
    }

    case Opcode::Weather: {
        const i32 amount = ex.popI32();
        const u32 seed = static_cast<u32>(ex.popI32());
        const u32 w = ex.canvas.width;
        const u32 h = ex.canvas.height;
        ex.spend(static_cast<u64>(w) * h);
        for (u32 y = 0; y < h; ++y) {
            for (u32 x = 0; x < w; ++x) {
                Color& c = ex.canvas.at(x, y);
                if (c.a == 0 || (c.r > 230 && c.g < 180)) {
                    continue;
                }
                u32 hv = seed ^ (x / 2u) * 73856093u ^ (y / 2u) * 19349663u;
                hv ^= hv >> 16u;
                hv *= 0x7feb352du;
                hv ^= hv >> 15u;
                if (hv % 4u != 0) {
                    continue;
                }
                // i64 arithmetic keeps the remainder defined for every
                // amount; the spec formula (spec §11.6) stays exact.
                const i32 delta =
                    static_cast<i32>(static_cast<i64>(hv >> 8u) % (static_cast<i64>(amount) * 2 + 1)) -
                    amount;
                const auto tone = [delta](u8 v) {
                    return static_cast<u8>(std::clamp(static_cast<i32>(v) + delta, 0, 255));
                };
                c = {tone(c.r), tone(c.g), tone(c.b), c.a};
            }
        }
        ++ex.frame().ip;
        return true;
    }

    default:
        return false;
    }
}

} // namespace iprop::detail
