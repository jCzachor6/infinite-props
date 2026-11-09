#include "iprop/VmInternals.hpp"
#include "iprop/math/Bezier.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <span>
#include <vector>

namespace iprop::detail {
namespace {

// Scaled, rounded coordinate (spec §8.1: multiply by the scale factor, then
// round to the nearest integer, half away from zero). Non-finite or
// out-of-range values trap: a cast out of i32 range would be UB.
i32 scaledCoord(Execution& ex, f32 value, f32 scale) {
    const f32 scaled = value * scale;
    if (!std::isfinite(scaled) || scaled < -2147483648.f || scaled >= 2147483648.f) {
        ex.trap("coordinate out of range", ErrorReason::NumericRange);
    }
    return static_cast<i32>(std::round(scaled));
}

// A raster coordinate: floor/ceil must stay within i32 (spec §11 formulas).
i32 coordToInt(Execution& ex, f32 value, const char* what) {
    if (!std::isfinite(value) || value < -2147483648.f || value >= 2147483648.f) {
        ex.trap(std::string("coordinate out of range (") + what + ")", ErrorReason::NumericRange);
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
        ex.trap("curve thickness out of range", ErrorReason::NumericRange);
    }
    if (thickness <= 0.f) {
        return;
    }
    for (std::size_t i = 0; i < count; ++i) {
        ex.work();
        points[i].x *= ex.canvas().scaleX;
        points[i].y *= ex.canvas().scaleY;
        if (!std::isfinite(points[i].x) || !std::isfinite(points[i].y)) {
            ex.trap("coordinate out of range (curve control)", ErrorReason::NumericRange);
        }
    }
    f32 left = points[0].x, right = left, top = points[0].y, bottom = top;
    for (std::size_t i = 1; i < count; ++i) {
        ex.work();
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
    const auto kernel = count == 3
        ? math::BezierKernel::quadratic(points[0], points[1], points[2])
        : math::BezierKernel::cubic(points[0], points[1], points[2], points[3]);
    const i32 clipX1 = std::min(x1, static_cast<i32>(ex.canvas().width) - 1);
    const i32 clipY1 = std::min(y1, static_cast<i32>(ex.canvas().height) - 1);
    for (i32 y = std::max(y0, 0); y <= clipY1; ++y) {
        ex.work();
        for (i32 x = std::max(x0, 0); x <= clipX1; ++x) {
            ex.work();
            const Vec2 p{static_cast<f32>(x), static_cast<f32>(y)};
            ex.work(25); // Five candidates with at most five Newton evaluations.
            const f32 distance = kernel.distanceTo(p);
            if (distance <= radius) {
                compositePixel(ex, x, y, colour);
            }
        }
    }
}

// Rows with at most this many edge crossings sort them (at most 496 moves).
constexpr std::size_t kSortedCrossings = 32;

// §11.2 point-in-polygon fill over scaled (f32) coordinates. Charges the
// bounding-box visit count up front (spec §8.3/§11.2).
//
// The rule is unchanged: a pixel is inside when an odd number of edges that
// straddle its centre row cross to the right of its centre, each crossing
// evaluated in the pinned operation order. A crossing depends only on the row,
// so it is computed once per row and the pixels compare against the sorted
// values. Work is still one step per row, per pixel visit and per edge of
// every visit, reserved before the pixel it belongs to.
void fillPolygon(Execution& ex, std::span<const Vec2> points, const Color& colour) {
    if (points.empty()) {
        return;
    }
    f32 left = points[0].x, right = points[0].x, top = points[0].y, bottom = points[0].y;
    for (const auto& p : points) {
        ex.work();
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
    std::array<f32, 16> fixed;
    std::vector<f32> large;
    if (n > fixed.size()) large.resize(n);
    f32* const crossings = n > fixed.size() ? large.data() : fixed.data();

    const u64 pixelSteps = 1u + static_cast<u64>(n); // the visit and its edges
    const u64 rowSteps = 1u + static_cast<u64>(static_cast<i64>(x1) - x0 + 1) * pixelSteps;
    const i64 lastColumn = static_cast<i64>(ex.canvas().width) - 1;
    const i64 lastRow = static_cast<i64>(ex.canvas().height) - 1;
    const bool clipped = !ex.target.clips.empty();
    for (i64 py = y0; py <= y1; ++py) {
        // Without clips a pixel cannot trap, so an affordable row is reserved
        // whole and only its on-canvas pixels are visited. Otherwise every
        // pixel reserves its own steps first, exactly as a per-edge loop.
        const bool prepaid = !clipped && ex.workBudget->affords(rowSteps);
        i64 from = x0, to = x1;
        if (prepaid) {
            ex.workEach(rowSteps);
            from = std::max<i64>(from, 0);
            to = std::min(to, lastColumn);
            if (py < 0 || py > lastRow || from > to) continue;
        } else {
            ex.work();
        }
        // Prepaid steps still poll at most kWorkPollInterval steps apart.
        u32 sincePoll = 0;
        const auto step = [&] {
            if (prepaid && ++sincePoll == kWorkPollInterval) { sincePoll = 0; ex.workBudget->poll(); }
        };

        const f32 cy = static_cast<f32>(py) + .5f;
        std::size_t count = 0;
        for (std::size_t i = 0, j = n - 1; i < n; j = i++) {
            step();
            const auto& a = points[i]; // current vertex
            const auto& d = points[j]; // previous vertex
            if ((a.y > cy) != (d.y > cy)) {
                // Pixel centres and the QuantSpace operation order, for
                // bit-compatible edge pixels (spec §11.2).
                const f32 crossing = (d.x - a.x) * (cy - a.y) / (d.y - a.y) + a.x;
                // No centre is left of a NaN crossing; it never counts.
                if (crossing == crossing) crossings[count++] = crossing;
            }
        }
        // A few crossings are sorted once: centres only move right, so the
        // crossings at or left of the centre form a growing prefix and the
        // rest are the edges it is left of. Many crossings are counted per
        // pixel instead, which never does more than the reserved edge steps.
        const bool sorted = count <= kSortedCrossings;
        for (std::size_t i = 1; sorted && i < count; ++i) {
            const f32 value = crossings[i];
            std::size_t at = i;
            for (; at > 0 && crossings[at - 1] > value; --at) crossings[at] = crossings[at - 1];
            crossings[at] = value;
        }
        std::size_t passed = 0;
        for (i64 px = from; px <= to; ++px) {
            if (prepaid) step();
            else ex.workEach(pixelSteps);
            const f32 cx = static_cast<f32>(px) + .5f;
            bool inside = false;
            if (sorted) {
                while (passed < count && !(cx < crossings[passed])) ++passed;
                inside = ((count - passed) & 1u) != 0;
            } else {
                for (std::size_t i = 0; i < count; ++i) { step(); inside ^= cx < crossings[i]; }
            }
            if (inside) {
                compositePixel(ex, static_cast<i32>(px), static_cast<i32>(py), colour);
            }
        }
    }
}

} // namespace

void compositePixel(Execution& ex, i32 x, i32 y, const Color& src, CompositionMode mode) {
    Canvas& canvas = ex.canvas();
    if (!canvas.contains(x, y)) {
        return;
    }
    Color& dst = canvas.at(static_cast<u32>(x), static_cast<u32>(y));
    if (ex.target.clips.empty()) {
        dst = composePixel(dst, src, resolveComposition(mode, ex.target.mode));
        return;
    }
    const auto coverage = clipCoverage(ex, x, y);
    if (!coverage) return;
    auto source = src;
    source.a = multiplyCoverage(source.a, coverage);
    dst = composePixel(dst, source, resolveComposition(mode, ex.target.mode));
}

// Canvas operations (spec §9.8, exact rasterization §11).
bool stepCanvas(Execution& ex, Instruction ins) {
    const f32 sX = ex.canvas().scaleX;
    const f32 sY = ex.canvas().scaleY;
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
        const std::array<Vec2, 4> corners{{{x, y}, {x + w, y}, {x + w, y + h}, {x, y + h}}};
        fillPolygon(ex, corners, colour);
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
                ex.work();
                const f32 t = static_cast<f32>(i) / static_cast<f32>(steps);
                const i32 ix = static_cast<i32>(std::round(ax + dx * t));
                const i32 iy = static_cast<i32>(std::round(ay + dy * t));
                for (i32 q = 0; q < thickness; ++q) {
                    ex.work();
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
            ex.work();
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
            ex.work();
            const f32 y = ex.popF32() * sY;
            const f32 x = ex.popF32() * sX;
            points[i - 1] = {x, y};
        }
        fillPolygon(ex, points, colour);
        ++ex.frame().ip;
        return true;
    }

    case Opcode::PolyList: {
        const auto colour = ex.popColor();
        const auto list = ex.popHandle<ListValue>("list handle");
        if (list.type != ValueType::list(ValueType::Vec2)) ex.trap("poly points must be list<vec2>", ErrorReason::StackType);
        if (list.data->elements.size() < 3) ex.trap("poly requires at least three vertices", ErrorReason::NumericRange);
        std::vector<Vec2> points;
        points.reserve(list.data->elements.size());
        for (const auto& value : list.data->elements) {
            ex.work();
            const auto point = std::get<Vec2>(value);
            points.push_back({point.x * sX, point.y * sY});
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
            ex.work();
            for (i32 px = x0; px <= x1; ++px) {
                ex.work();
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
        const u32 w = ex.canvas().width;
        const u32 h = ex.canvas().height;
        ex.spend(static_cast<u64>(w) * h);
        for (u32 y = 0; y < h; ++y) {
            ex.work();
            for (u32 x = 0; x < w; ++x) {
                ex.work();
                Color& c = ex.canvas().at(x, y);
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
                weatherPixel(ex, static_cast<i32>(x), static_cast<i32>(y),
                    {tone(c.r), tone(c.g), tone(c.b), c.a});
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
