#include "iprop/VmInternals.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace iprop::detail {
namespace {

// Scaled, rounded coordinate (spec §8.1: multiply by the scale factor, then
// round to the nearest integer, half away from zero).
i32 scaledCoord(f32 value, f32 scale) {
    return static_cast<i32>(std::round(value * scale));
}

// §11.1: out-of-bounds writes are no-ops; opaque colours overwrite;
// translucent colours blend per channel with the destination alpha kept.
void putPixel(Execution& ex, i32 x, i32 y, const Color& src) {
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
    const i32 x0 = static_cast<i32>(std::floor(left));
    const i32 x1 = static_cast<i32>(std::ceil(right));
    const i32 y0 = static_cast<i32>(std::floor(top));
    const i32 y1 = static_cast<i32>(std::ceil(bottom));
    const u64 visits =
        static_cast<u64>(x1 - x0 + 1) * static_cast<u64>(y1 - y0 + 1);
    ex.spend(visits);

    const std::size_t n = points.size();
    for (i32 py = y0; py <= y1; ++py) {
        for (i32 px = x0; px <= x1; ++px) {
            bool inside = false;
            for (std::size_t i = 0; i < n; ++i) {
                const std::size_t j = (i + n - 1) % n;
                const auto& a = points[j];
                const auto& b = points[i];
                if ((a.y > static_cast<f32>(py) + .5f) !=
                        (b.y > static_cast<f32>(py) + .5f) &&
                    static_cast<f32>(px) + .5f <
                        (b.x - a.x) * (static_cast<f32>(py) + .5f - a.y) / (b.y - a.y) + a.x) {
                    inside = !inside;
                }
            }
            if (inside) {
                putPixel(ex, px, py, colour);
            }
        }
    }
}

} // namespace

// Canvas operations (spec §9.8, exact rasterization §11).
bool stepCanvas(Execution& ex, Instruction ins) {
    const f32 sX = ex.canvas.scaleX;
    const f32 sY = ex.canvas.scaleY;
    switch (ins.opcode) {
    case Opcode::Pixel: {
        const Color colour = ex.popColor();
        const i32 y = scaledCoord(static_cast<f32>(ex.popI32()), sY);
        const i32 x = scaledCoord(static_cast<f32>(ex.popI32()), sX);
        putPixel(ex, x, y, colour);
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
        const i32 steps = std::max(1, static_cast<i32>(std::ceil(std::max(std::fabs(dx), std::fabs(dy)))));
        if (thickness > 0) {
            ex.spend(static_cast<u64>(steps + 1) * static_cast<u64>(thickness));
            for (i32 i = 0; i <= steps; ++i) {
                const f32 t = static_cast<f32>(i) / static_cast<f32>(steps);
                const i32 ix = static_cast<i32>(std::round(ax + dx * t));
                const i32 iy = static_cast<i32>(std::round(ay + dy * t));
                for (i32 q = 0; q < thickness; ++q) {
                    putPixel(ex, ix, iy + q, colour);
                }
            }
        }
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
        const i32 y0 = static_cast<i32>(std::floor(y - ry));
        const i32 y1 = static_cast<i32>(std::ceil(y + ry));
        const i32 x0 = static_cast<i32>(std::floor(x - rx));
        const i32 x1 = static_cast<i32>(std::ceil(x + rx));
        ex.spend(static_cast<u64>(x1 - x0 + 1) * static_cast<u64>(y1 - y0 + 1));
        for (i32 py = y0; py <= y1; ++py) {
            for (i32 px = x0; px <= x1; ++px) {
                const f32 nx = (static_cast<f32>(px) - x) / rx;
                const f32 ny = (static_cast<f32>(py) - y) / ry;
                if (nx * nx + ny * ny <= 1.f) {
                    putPixel(ex, px, py, colour);
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
