#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

#include "support/test_support.hpp"

#include "iprop/Canvas.hpp"
#include "iprop/VM.hpp"
#include "iprop/math/Bezier.hpp"

// Canvas operations (spec §9.8, exact rasterization §11). Outputs are
// compared against reference implementations of the spec pseudocode; the
// alpha rule and fuel visit counts are pinned.

using namespace iprop;
using namespace test_support;

namespace {

// ---- Test-side references (spec §11 pseudocode, written independently) ----

Color blendRef(Color src, Color dst) {
    if (src.a == 255) {
        return src;
    }
    const u32 a = src.a;
    const u32 b = 255u - a;
    return {static_cast<u8>((static_cast<u32>(src.r) * a + static_cast<u32>(dst.r) * b) / 255u),
            static_cast<u8>((static_cast<u32>(src.g) * a + static_cast<u32>(dst.g) * b) / 255u),
            static_cast<u8>((static_cast<u32>(src.b) * a + static_cast<u32>(dst.b) * b) / 255u),
            dst.a};
}

struct RefCanvas {
    u32 w = 0, h = 0;
    std::vector<Color> pixels;

    explicit RefCanvas(u32 width, u32 height)
        : w(width), h(height), pixels(static_cast<std::size_t>(width) * height) {}

    bool inside(i32 x, i32 y) const {
        return x >= 0 && y >= 0 && static_cast<u32>(x) < w && static_cast<u32>(y) < h;
    }
    Color& at(i32 x, i32 y) { return pixels[static_cast<std::size_t>(y) * w + x]; }
    const Color& at(i32 x, i32 y) const {
        return pixels[static_cast<std::size_t>(y) * w + x];
    }

    void pixel(i32 x, i32 y, Color c) {
        if (!inside(x, y)) return;
        at(x, y) = blendRef(c, at(x, y));
    }

    void poly(const std::vector<Vec2>& pts, Color c) {
        if (pts.empty()) return;
        f32 l = pts[0].x, r = pts[0].x, t = pts[0].y, b = pts[0].y;
        for (const auto& p : pts) {
            l = std::min(l, p.x); r = std::max(r, p.x);
            t = std::min(t, p.y); b = std::max(b, p.y);
        }
        const i32 x0 = static_cast<i32>(std::floor(l)), x1 = static_cast<i32>(std::ceil(r));
        const i32 y0 = static_cast<i32>(std::floor(t)), y1 = static_cast<i32>(std::ceil(b));
        const std::size_t n = pts.size();
        for (i32 py = y0; py <= y1; ++py)
            for (i32 px = x0; px <= x1; ++px) {
                bool inside = false;
                for (std::size_t i = 0; i < n; ++i) {
                    const std::size_t j = (i + n - 1) % n;
                    const auto& a = pts[i];
                    const auto& d = pts[j];
                    const float cx = static_cast<float>(px) + .5f;
                    const float cy = static_cast<float>(py) + .5f;
                    if ((a.y > cy) != (d.y > cy) &&
                        cx < (d.x - a.x) * (cy - a.y) / (d.y - a.y) + a.x) {
                        inside = !inside;
                    }
                }
                if (inside) pixel(px, py, c);
            }
    }

    void line(Vec2 a, Vec2 b, i32 thickness, Color c) {
        const f32 dx = b.x - a.x, dy = b.y - a.y;
        const i32 steps = std::max(1, static_cast<i32>(std::ceil(std::max(std::fabs(dx), std::fabs(dy)))));
        for (i32 i = 0; i <= steps; ++i) {
            const f32 t = static_cast<f32>(i) / static_cast<f32>(steps);
            const i32 ix = static_cast<i32>(std::round(a.x + dx * t));
            const i32 iy = static_cast<i32>(std::round(a.y + dy * t));
            for (i32 q = 0; q < thickness; ++q) pixel(ix, iy + q, c);
        }
    }

    void oval(Vec2 center, Vec2 radii, Color c) {
        const i32 y0 = static_cast<i32>(std::floor(center.y - radii.y));
        const i32 y1 = static_cast<i32>(std::ceil(center.y + radii.y));
        const i32 x0 = static_cast<i32>(std::floor(center.x - radii.x));
        const i32 x1 = static_cast<i32>(std::ceil(center.x + radii.x));
        for (i32 py = y0; py <= y1; ++py)
            for (i32 px = x0; px <= x1; ++px) {
                const f32 nx = (static_cast<f32>(px) - center.x) / radii.x;
                const f32 ny = (static_cast<f32>(py) - center.y) / radii.y;
                if (nx * nx + ny * ny <= 1.f) pixel(px, py, c);
            }
    }

    void weather(u32 seed, i32 amount) {
        for (u32 y = 0; y < h; ++y)
            for (u32 x = 0; x < w; ++x) {
                Color& c = at(static_cast<i32>(x), static_cast<i32>(y));
                if (c.a == 0 || (c.r > 230 && c.g < 180)) continue;
                u32 hv = seed ^ (x / 2u) * 73856093u ^ (y / 2u) * 19349663u;
                hv ^= hv >> 16u; hv *= 0x7feb352du; hv ^= hv >> 15u;
                if (hv % 4u != 0) continue;
                const i32 delta =
                    static_cast<i32>(static_cast<i64>(hv >> 8u) % (static_cast<i64>(amount) * 2 + 1)) - amount;
                const auto tone = [delta](u8 v) {
                    return static_cast<u8>(std::clamp(static_cast<i32>(v) + delta, 0, 255));
                };
                c = {tone(c.r), tone(c.g), tone(c.b), c.a};
            }
    }
};

// The per-pixel loop of spec §11.2 with its work steps: one per row, one per
// pixel visit and one per edge of every visit. It stops, as a work limit
// does, before the first step it cannot afford; a pixel is written only
// after all of its steps.
void polyWithin(RefCanvas& ref, const std::vector<Vec2>& pts, Color c, u64 steps) {
    f32 l = pts[0].x, r = pts[0].x, t = pts[0].y, b = pts[0].y;
    for (const auto& p : pts) {
        l = std::min(l, p.x); r = std::max(r, p.x);
        t = std::min(t, p.y); b = std::max(b, p.y);
    }
    const i32 x0 = static_cast<i32>(std::floor(l)), x1 = static_cast<i32>(std::ceil(r));
    const i32 y0 = static_cast<i32>(std::floor(t)), y1 = static_cast<i32>(std::ceil(b));
    const std::size_t n = pts.size();
    for (i32 py = y0; py <= y1; ++py) {
        if (steps < 1) return;
        steps -= 1;
        for (i32 px = x0; px <= x1; ++px) {
            if (steps < 1 + n) return;
            steps -= 1 + n;
            bool inside = false;
            for (std::size_t i = 0; i < n; ++i) {
                const auto& a = pts[i];
                const auto& d = pts[(i + n - 1) % n];
                const float cx = static_cast<float>(px) + .5f;
                const float cy = static_cast<float>(py) + .5f;
                if ((a.y > cy) != (d.y > cy) &&
                    cx < (d.x - a.x) * (cy - a.y) / (d.y - a.y) + a.x) {
                    inside = !inside;
                }
            }
            if (inside) ref.pixel(px, py, c);
        }
    }
}

u64 polySteps(const std::vector<Vec2>& pts) {
    f32 l = pts[0].x, r = pts[0].x, t = pts[0].y, b = pts[0].y;
    for (const auto& p : pts) {
        l = std::min(l, p.x); r = std::max(r, p.x);
        t = std::min(t, p.y); b = std::max(b, p.y);
    }
    const u64 columns = static_cast<u64>(static_cast<i64>(std::ceil(r)) - static_cast<i64>(std::floor(l)) + 1);
    const u64 rows = static_cast<u64>(static_cast<i64>(std::ceil(b)) - static_cast<i64>(std::floor(t)) + 1);
    return rows * (1 + columns * (1 + pts.size()));
}

// Deterministic vertices on a quarter-pixel grid around a 32x32 canvas: edges
// pass through pixel centres, leave the canvas and cross one another.
struct Shapes {
    u32 state = 0x2545f491u;
    u32 next() { state ^= state << 13; state ^= state >> 17; state ^= state << 5; return state; }
    std::vector<Vec2> scattered(std::size_t n) {
        std::vector<Vec2> pts(n);
        for (auto& p : pts)
            p = {static_cast<f32>(static_cast<i32>(next() % 177u) - 24) / 4.f,
                 static_cast<f32>(static_cast<i32>(next() % 177u) - 24) / 4.f};
        return pts;
    }
    // Every row crosses nearly every edge: more crossings than a row sorts.
    std::vector<Vec2> zigzag(std::size_t n) {
        std::vector<Vec2> pts(n);
        for (std::size_t i = 0; i < n; ++i)
            pts[i] = {1.f + 29.f * static_cast<f32>(i) / static_cast<f32>(n) + static_cast<f32>(next() % 3u) / 4.f,
                      (i & 1u) != 0 ? 30.25f - static_cast<f32>(next() % 5u) : 1.5f + static_cast<f32>(next() % 5u)};
        return pts;
    }
};

Prog polyProg(const std::vector<Vec2>& pts, Color c) {
    Prog prog;
    for (const auto& p : pts) prog.F(p.x).F(p.y);
    prog.C(c).emit(0x83, static_cast<u16>(pts.size()));
    return prog;
}

struct LimitedRun {
    Canvas canvas;
    u64 work = 0;
    bool exhausted = false;
};

// Runs unscaled on a 32x32 canvas; a failed run keeps the canvas it reached.
LimitedRun runLimited(const Prog& prog, u64 workLimit = 0) {
    const auto bytes = buildProgFile(prog, 32, 32);
    auto loaded = loadFile(bytes);
    GenerationInputs inputs;
    inputs.limits.workLimit = workLimit;
    VM vm(loaded, inputs);
    LimitedRun run;
    try {
        vm.run();
    } catch (const Trap& error) {
        REQUIRE(error.reason == ErrorReason::WorkExhausted);
        run.exhausted = true;
    }
    run.canvas = vm.canvas();
    run.work = vm.workBudget() - vm.work();
    return run;
}

// Asserts the VM canvas equals the reference canvas, pixel by pixel.
void requireSame(const Canvas& canvas, const RefCanvas& ref) {
    REQUIRE(canvas.width == ref.w);
    REQUIRE(canvas.height == ref.h);
    for (u32 y = 0; y < ref.h; ++y) {
        for (u32 x = 0; x < ref.w; ++x) {
            INFO("pixel " << x << "," << y);
            REQUIRE(canvas.at(x, y) == ref.at(static_cast<i32>(x), static_cast<i32>(y)));
        }
    }
}

} // namespace

TEST_CASE("pixel writes opaque colours and clips out-of-bounds") {
    const Color red{200, 10, 10, 255};
    Prog prog;
    prog.I(2).I(3).C(red).emit(0x80);  // pixel(2, 3)
    prog.I(-5).I(0).C(red).emit(0x80); // outside: no-op
    prog.I(0).I(99).C(red).emit(0x80); // outside: no-op

    const auto canvas = runProgCanvas(prog);
    RefCanvas ref(8, 8);
    ref.pixel(2, 3, red);
    requireSame(canvas, ref);
}

TEST_CASE("quadratic and cubic strokes have finite round caps") {
    const Color red{255, 0, 0, 255};
    for (const bool cubic : {false, true}) {
        Prog prog;
        prog.F(1.f).F(3.f).emit(0x5B).F(3.f).F(3.f).emit(0x5B);
        if (cubic) prog.F(3.f).F(3.f).emit(0x5B);
        prog.F(5.f).F(3.f).emit(0x5B).F(2.f).C(red).emit(cubic ? 0x87 : 0x86);
        const auto canvas = runProgCanvas(prog);
        for (i32 y = 0; y < 8; ++y) {
            for (i32 x = 0; x < 8; ++x) {
                const i32 dx = x < 1 ? 1 - x : x > 5 ? x - 5 : 0;
                const i32 dy = y - 3;
                const bool covered = dx * dx + dy * dy <= 1;
                INFO("cubic=" << cubic << " pixel=" << x << "," << y);
                REQUIRE(canvas.at(static_cast<u32>(x), static_cast<u32>(y)) ==
                        (covered ? red : Color{}));
            }
        }
    }
}

TEST_CASE("Bezier strokes follow curved geometry rather than the endpoint chord") {
    const Color blue{0, 0, 255, 255};
    for (const bool cubic : {false, true}) {
        Prog prog;
        prog.F(1.f).F(5.f).emit(0x5B);
        if (cubic) {
            prog.F(1.f).F(1.f).emit(0x5B).F(5.f).F(1.f).emit(0x5B);
        } else {
            prog.F(3.f).F(1.f).emit(0x5B);
        }
        prog.F(5.f).F(5.f).emit(0x5B).F(0.25f).C(blue).emit(cubic ? 0x87 : 0x86);
        const auto canvas = runProgCanvas(prog);
        REQUIRE(canvas.at(1, 5) == blue);
        REQUIRE(canvas.at(5, 5) == blue);
        REQUIRE(canvas.at(3, cubic ? 2 : 3) == blue);
        REQUIRE(canvas.at(3, 5) == Color{});
    }
}

TEST_CASE("curve scaling preserves pixel thickness and a constant curve becomes a disk") {
    const Color green{0, 255, 0, 255};
    Prog line;
    line.F(1.f).F(3.f).emit(0x5B).F(3.f).F(3.f).emit(0x5B)
        .F(5.f).F(3.f).emit(0x5B).F(2.f).C(green).emit(0x86);
    const auto scaled = runProgCanvas(line, 16, 24);
    REQUIRE(scaled.at(2, 9) == green);
    REQUIRE(scaled.at(10, 9) == green);
    REQUIRE(scaled.at(6, 8) == green);
    REQUIRE(scaled.at(6, 7) == Color{});
    REQUIRE(scaled.at(12, 9) == Color{});
    Prog point;
    for (int i = 0; i < 4; ++i) point.F(2.f).F(2.f).emit(0x5B);
    point.F(2.f).C(green).emit(0x87);
    const auto disk = runProgCanvas(point);
    REQUIRE(disk.at(2, 2) == green);
    REQUIRE(disk.at(3, 2) == green);
    REQUIRE(disk.at(3, 3) == Color{});
}

TEST_CASE("curve fuel charges the complete control box before drawing") {
    Prog prog;
    // Expanded box [0,6] x [2,4]: 21 visits plus 12 instructions.
    prog.F(1.f).F(3.f).emit(0x5B).F(3.f).F(3.f).emit(0x5B)
        .F(5.f).F(3.f).emit(0x5B).F(2.f).C({255, 0, 0, 255}).emit(0x86);
    const auto bytes = buildProgFile(prog);
    const auto loaded = loadFile(bytes);
    VM vm(loaded, {});
    vm.run();
    REQUIRE(vm.fuelBudget() - vm.fuel() == 28); // eleven arguments + ceil(22 * .75)
    Prog huge;
    huge.F(-1000.f).F(-1000.f).emit(0x5B).F(0.f).F(0.f).emit(0x5B)
        .F(1000.f).F(1000.f).emit(0x5B).F(1.f).C({255, 0, 0, 255}).emit(0x86);
    const auto hugeBytes = buildProgFile(huge);
    const auto hugeLoaded = loadFile(hugeBytes);
    VM limited(hugeLoaded, {});
    REQUIRE_THROWS_AS(limited.run(), Trap);
    REQUIRE(std::all_of(limited.canvas().pixels.begin(), limited.canvas().pixels.end(),
                        [](Color c) { return c == Color{}; }));
}

TEST_CASE("curve thickness and coordinates are validated before raster conversion") {
    for (const f32 thickness : {0.f, -1.f, std::numeric_limits<f32>::infinity()}) {
        Prog prog;
        for (int i = 0; i < 3; ++i) prog.F(2.f).F(2.f).emit(0x5B);
        prog.F(thickness).C({255, 0, 0, 255}).emit(0x86);
        if (std::isfinite(thickness)) {
            const auto canvas = runProgCanvas(prog);
            REQUIRE(std::all_of(canvas.pixels.begin(), canvas.pixels.end(),
                                [](Color c) { return c == Color{}; }));
        } else {
            expectProgTrap(prog, "curve thickness out of range");
        }
    }
    Prog badControl;
    badControl.F(std::numeric_limits<f32>::infinity()).F(0.f).emit(0x5B)
        .F(1.f).F(1.f).emit(0x5B).F(2.f).F(2.f).emit(0x5B)
        .F(1.f).C({255, 0, 0, 255}).emit(0x86);
    expectProgTrap(badControl, "coordinate out of range");
}

TEST_CASE("the alpha rule blends channels and keeps the destination alpha") {
    // {255,0,0,128} over {0,255,0,255}: r=(255*128+0*127)/255=128,
    // g=(0*128+255*127)/255=127, b=0, alpha stays 255.
    const Color src{255, 0, 0, 128};
    const Color dst{0, 255, 0, 255};
    Prog prog;
    prog.I(1).I(1).C(dst).emit(0x80);
    prog.I(1).I(1).C(src).emit(0x80);
    const auto canvas = runProgCanvas(prog);
    REQUIRE(canvas.at(1, 1) == Color{128, 127, 0, 255});

    // Blending onto a transparent pixel keeps it transparent (alpha 0)
    // while the channels still accumulate — the QuantSpace semantics.
    Prog translucent;
    translucent.I(0).I(0).C(Color{0, 0, 255, 64}).emit(0x80);
    const auto canvas2 = runProgCanvas(translucent);
    REQUIRE(canvas2.at(0, 0) == Color{0, 0, 64, 0});
}

TEST_CASE("rect fills exactly like the reference polygon") {
    const Color c{9, 88, 77, 255};
    Prog prog;
    prog.F(1.f).F(2.f).F(4.f).F(3.f).C(c).emit(0x81); // rect(1, 2, 4, 3)
    const auto canvas = runProgCanvas(prog);

    RefCanvas ref(8, 8);
    ref.poly({{1, 2}, {5, 2}, {5, 5}, {1, 5}}, c);
    requireSame(canvas, ref);
}

TEST_CASE("line rasterizes per the spec (steps and thickness)") {
    const Color c{1, 2, 3, 255};
    Prog prog;
    prog.F(0.f).F(0.f).F(3.f).F(0.f).I(2).C(c).emit(0x82); // line(0,0 → 3,0, thickness 2)
    const auto canvas = runProgCanvas(prog);

    RefCanvas ref(8, 8);
    ref.line({0, 0}, {3, 0}, 2, c);
    requireSame(canvas, ref);

    // Hand-pinned expectation: 8 pixels, two rows.
    REQUIRE(canvas.at(0, 0) == c);
    REQUIRE(canvas.at(3, 0) == c);
    REQUIRE(canvas.at(0, 1) == c);
    REQUIRE(canvas.at(3, 1) == c);
    REQUIRE(canvas.at(4, 0) == Color{0, 0, 0, 0});
}

TEST_CASE("poly fills by the point-in-polygon rule") {
    const Color c{5, 6, 7, 255};
    Prog prog;
    prog.F(0.f).F(0.f).F(4.f).F(0.f).F(0.f).F(4.f).C(c).emit(0x83, 3); // triangle
    const auto canvas = runProgCanvas(prog);

    RefCanvas ref(8, 8);
    ref.poly({{0, 0}, {4, 0}, {0, 4}}, c);
    requireSame(canvas, ref);
}

TEST_CASE("polygon rows reproduce the per-pixel edge rule for every shape class") {
    Shapes shapes;
    const std::size_t counts[] = {3, 4, 5, 8, 17, 33, 64};
    for (u32 shape = 0; shape < 140; ++shape) {
        const std::size_t n = counts[shape % 7];
        const auto pts = shape % 5 == 4 ? shapes.zigzag(n) : shapes.scattered(n);
        // A translucent colour exposes a pixel written twice or not at all.
        const Color c{200, 40, 90, static_cast<u8>(shape % 3 == 0 ? 255 : 120)};
        INFO("shape " << shape << " with " << n << " vertices");
        const auto run = runLimited(polyProg(pts, c));
        REQUIRE_FALSE(run.exhausted);
        RefCanvas ref(32, 32);
        ref.poly(pts, c);
        requireSame(run.canvas, ref);
    }
}

TEST_CASE("polygon work is one step per row, visit and edge, and a limit stops at the same pixel") {
    Shapes shapes;
    for (const std::size_t n : {3u, 4u, 17u, 40u}) {
        for (const bool crossing : {false, true}) {
            const auto pts = crossing ? shapes.zigzag(n) : shapes.scattered(n);
            const Color c{30, 200, 110, 140};
            INFO(n << " vertices, crossing " << crossing);
            const auto prog = polyProg(pts, c);
            const auto full = runLimited(prog);
            REQUIRE_FALSE(full.exhausted);

            // Dropping the operands instead dispatches 2n more instructions
            // than the fill, which pops n vertices and scans n for its box.
            Prog dropped;
            for (const auto& p : pts) dropped.F(p.x).F(p.y);
            dropped.C(c);
            for (std::size_t i = 0; i < 2 * n + 1; ++i) dropped.emit(0x16);
            const u64 fill = polySteps(pts);
            REQUIRE(full.work == runLimited(dropped).work + fill);

            // The entry's implicit return is the one step after the fill.
            const u64 before = full.work - 1 - fill;
            for (const u64 afforded : {u64{0}, u64{1}, u64{2}, u64{n + 1}, u64{n + 2}, u64{3 * (n + 1) + 1},
                                       fill / 3, fill / 2 + 1, fill - (n + 1), fill - 1, fill}) {
                INFO("afforded " << afforded << " of " << fill);
                const auto stopped = runLimited(prog, before + afforded);
                REQUIRE(stopped.exhausted);
                REQUIRE(stopped.work == before + afforded);
                RefCanvas ref(32, 32);
                polyWithin(ref, pts, c, afforded);
                requireSame(stopped.canvas, ref);
            }
        }
    }
}

TEST_CASE("strokes equal the per-pixel distance test, however much of the box is skipped") {
    Shapes shapes;
    const Color c{250, 250, 20, 255};
    for (u32 shape = 0; shape < 120; ++shape) {
        const bool cubic = shape % 2 != 0;
        auto pts = shapes.scattered(cubic ? 4 : 3);
        if (shape % 11 == 3) pts[1] = pts[0];              // a doubled control point
        if (shape % 11 == 7) for (auto& p : pts) p = pts[0]; // a constant curve: a disk
        if (shape % 13 == 5) for (auto& p : pts) p = {p.x * 3.f, p.y * 3.f}; // mostly off canvas
        const f32 thickness = static_cast<f32>(1u + shapes.next() % 24u) / 4.f + (shape % 17 == 0 ? 21.f : 0.f);
        INFO("shape " << shape << ", thickness " << thickness);

        Prog prog;
        for (const auto& p : pts) prog.F(p.x).F(p.y).emit(0x5B);
        prog.F(thickness).C(c).emit(cubic ? 0x87 : 0x86);
        const auto run = runLimited(prog);
        REQUIRE_FALSE(run.exhausted);

        const auto kernel = cubic ? math::BezierKernel::cubic(pts[0], pts[1], pts[2], pts[3])
                                  : math::BezierKernel::quadratic(pts[0], pts[1], pts[2]);
        const f32 radius = thickness * 0.5f;
        RefCanvas ref(32, 32);
        for (i32 y = 0; y < 32; ++y)
            for (i32 x = 0; x < 32; ++x)
                if (kernel.distanceTo({static_cast<f32>(x), static_cast<f32>(y)}) <= radius) ref.pixel(x, y, c);
        requireSame(run.canvas, ref);
    }
}

TEST_CASE("oval fills per the ellipse test") {
    const Color c{11, 22, 33, 255};
    Prog prog;
    prog.F(2.f).F(2.f).F(2.f).F(2.f).C(c).emit(0x84); // oval(2,2, r=2,2)
    const auto canvas = runProgCanvas(prog);

    RefCanvas ref(8, 8);
    ref.oval({2, 2}, {2, 2}, c);
    requireSame(canvas, ref);
}

TEST_CASE("weather matches the spec pseudocode on a filled canvas") {
    Prog prog;
    prog.F(0.f).F(0.f).F(8.f).F(8.f).C(Color{100, 100, 100, 255}).emit(0x81); // full rect
    prog.I(42).I(13).emit(0x85); // weather(42, 13)
    const auto canvas = runProgCanvas(prog);

    RefCanvas ref(8, 8);
    for (u32 y = 0; y < 8; ++y)
        for (u32 x = 0; x < 8; ++x) ref.at(static_cast<i32>(x), static_cast<i32>(y)) = {100, 100, 100, 255};
    ref.weather(42, 13);
    requireSame(canvas, ref);
}

TEST_CASE("weather skips transparent and bright warm pixels") {
    // A red pixel (r > 230, g < 180) and a transparent pixel stay untouched;
    // a blue pixel may change.
    Prog prog;
    prog.I(0).I(0).C(Color{255, 0, 0, 255}).emit(0x80);
    prog.I(1).I(1).C(Color{0, 0, 255, 255}).emit(0x80);
    prog.I(7).I(13).emit(0x85);
    const auto canvas = runProgCanvas(prog);
    REQUIRE(canvas.at(0, 0) == Color{255, 0, 0, 255});
    REQUIRE(canvas.at(0, 1) == Color{0, 0, 0, 0}); // untouched transparent pixel
}

TEST_CASE("fuel costs match the §11 visit formulas") {
    // poly with a 4x4 bounding box: 1 + 25 visits.
    {
        Prog prog;
        prog.F(0.f).F(0.f).F(4.f).F(0.f).F(0.f).F(4.f).C(Color{1, 1, 1, 255}).emit(0x83, 3);
        const auto bytes = buildProgFile(prog);
        auto loaded = loadFile(bytes);
        VM vm(loaded, {});
        vm.run();
        // 6 const.f32 + 1 const.color + 1 poly(1+25) + implicit end.
        REQUIRE(vm.fuel() == 100000 - (6 + 1 + 20)); // ceil(26 * .75)
    }
    // weather on the 8x8 canvas: 1 + 64 visits.
    {
        Prog prog;
        prog.I(0).I(13).emit(0x85);
        const auto bytes = buildProgFile(prog);
        auto loaded = loadFile(bytes);
        VM vm(loaded, {});
        vm.run();
        REQUIRE(vm.fuel() == 100000 - (2 + 65));
    }
    // A huge poly traps with fuel exhausted, not a silent undercount.
    {
        Prog prog;
        prog.F(0.f).F(0.f).F(1000.f).F(0.f).F(0.f).F(1000.f).C(Color{1, 1, 1, 255}).emit(0x83, 3);
        expectProgTrap(prog, "fuel exhausted");
    }
}

TEST_CASE("coordinates scale to the requested canvas size") {
    const Color c{7, 7, 7, 255};
    Prog prog;
    prog.I(4).I(4).C(c).emit(0x80); // asset (4,4) on a 8x8 base
    // Requested 16x8: sX = 2, sY = 1 -> target pixel (8, 4).
    const auto canvas = runProgCanvas(prog, 16, 8);
    REQUIRE(canvas.width == 16);
    REQUIRE(canvas.height == 8);
    REQUIRE(canvas.at(8, 4) == c);
    REQUIRE(canvas.at(7, 4) == Color{0, 0, 0, 0});
    REQUIRE(canvas.at(9, 4) == Color{0, 0, 0, 0});
}
