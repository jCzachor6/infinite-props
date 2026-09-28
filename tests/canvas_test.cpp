#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#include "support/test_support.hpp"

#include "iprop/Canvas.hpp"
#include "iprop/VM.hpp"

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
                    const auto& a = pts[j];
                    const auto& b = pts[i];
                    if ((a.y > static_cast<f32>(py) + .5f) !=
                            (b.y > static_cast<f32>(py) + .5f) &&
                        static_cast<f32>(px) + .5f <
                            (b.x - a.x) * (static_cast<f32>(py) + .5f - a.y) / (b.y - a.y) + a.x) {
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
        REQUIRE(vm.fuel() == 100000 - (6 + 1 + 26));
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
