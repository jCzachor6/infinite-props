#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <set>
#include <string>

#include <vector>

#include "support/test_support.hpp"
#include "support/f32_corpus.hpp"
#include "support/math_support.hpp"

#include "iprop/VM.hpp"

using namespace iprop;
using namespace test_support;
using namespace math_test;

namespace {

f32 evalBezier(Vec2 p, std::initializer_list<Vec2> controls) {
    Prog prog;
    prog.F(p.x).F(p.y).emit(0x5B);
    for (const auto& control : controls) {
        prog.F(control.x).F(control.y).emit(0x5B);
    }
    prog.emit(controls.size() == 3 ? 0x7E : 0x88);
    return evalProgF32(prog);
}

} // namespace

TEST_CASE("the signed-distance fields follow the pinned formulas exactly") {
    // dist.circle: the centre is -r; {3,4} from {0,0} is 5 exactly.
    REQUIRE(evalProgF32(Prog{}.F(0.f).F(0.f).emit(0x5B).F(0.f).F(0.f).emit(0x5B)
                                .F(3.f).emit(0x7A)) == -3.f);
    REQUIRE(evalProgF32(Prog{}.F(3.f).F(4.f).emit(0x5B).F(0.f).F(0.f).emit(0x5B)
                                .F(0.f).emit(0x7A)) == 5.f);

    // dist.rect: inside the negative max(dx, dy); outside the corner
    // distance; the edge is 0.
    REQUIRE(evalProgF32(Prog{}.F(0.f).F(0.f).emit(0x5B).F(0.f).F(0.f).emit(0x5B)
                                .F(2.f).F(3.f).emit(0x5B).emit(0x7B)) == -2.f);
    REQUIRE(evalProgF32(Prog{}.F(10.f).F(0.f).emit(0x5B).F(0.f).F(0.f).emit(0x5B)
                                .F(2.f).F(3.f).emit(0x5B).emit(0x7B)) == 8.f);
    REQUIRE(evalProgF32(Prog{}.F(2.f).F(0.f).emit(0x5B).F(0.f).F(0.f).emit(0x5B)
                                .F(2.f).F(3.f).emit(0x5B).emit(0x7B)) == 0.f);
    // The corner: {5, 5} outside a 2x3 half box centred at 0 — the
    // distance from (2,3) is sqrt(9 + 4) = sqrt(13).
    REQUIRE(evalProgF32(Prog{}.F(5.f).F(5.f).emit(0x5B).F(0.f).F(0.f).emit(0x5B)
                                .F(2.f).F(3.f).emit(0x5B).emit(0x7B)) ==
            std::sqrt(13.f));

    // dist.line: on the segment 0; beyond the end clamps.
    REQUIRE(evalProgF32(Prog{}.F(2.f).F(0.f).emit(0x5B).F(0.f).F(0.f).emit(0x5B)
                                .F(4.f).F(0.f).emit(0x5B).emit(0x7D)) == 0.f);
    REQUIRE(evalProgF32(Prog{}.F(-2.f).F(0.f).emit(0x5B).F(0.f).F(0.f).emit(0x5B)
                                .F(4.f).F(0.f).emit(0x5B).emit(0x7D)) == 2.f);

    // dist.bezier: the endpoints are exact zeros.
    REQUIRE(evalProgF32(Prog{}.F(0.f).F(0.f).emit(0x5B).F(0.f).F(0.f).emit(0x5B)
                                .F(1.f).F(1.f).emit(0x5B).F(2.f).F(0.f).emit(0x5B)
                                .emit(0x7E)) == 0.f);
    REQUIRE(evalProgF32(Prog{}.F(2.f).F(0.f).emit(0x5B).F(0.f).F(0.f).emit(0x5B)
                                .F(1.f).F(1.f).emit(0x5B).F(2.f).F(0.f).emit(0x5B)
                                .emit(0x7E)) == 0.f);

    // fill: inside 1, outside 0, the isocontour 0.5.
    REQUIRE(evalProgF32(Prog{}.F(-1.f).F(2.f).emit(0x7F)) == 1.f);
    REQUIRE(evalProgF32(Prog{}.F(1.f).F(2.f).emit(0x7F)) == 0.f);
    REQUIRE(evalProgF32(Prog{}.F(0.f).F(2.f).emit(0x7F)) == 0.5f);
    REQUIRE(evalProgF32(Prog{}.F(-0.5f).F(2.f).emit(0x7F)) == 0.84375f);
}

TEST_CASE("dist.circle and dist.rect agree with the vec2/f32 composition") {
    // dist.circle(p, c, r) == len(p - c) - r, bit for bit.
    const auto composed = evalProg(Prog{}.F(1.5f).F(2.5f).emit(0x5B)
                                        .F(-1.f).F(3.f).emit(0x5B)
                                        .emit(0x5D)   // p - c
                                        .emit(0x75)   // len
                                        .F(2.f).emit(0x41)); // - r
    const auto direct = evalProg(Prog{}.F(1.5f).F(2.5f).emit(0x5B)
                                       .F(-1.f).F(3.f).emit(0x5B)
                                       .F(2.f).emit(0x7A));
    REQUIRE(bitsOf(std::get<f32>(composed[0])) == bitsOf(std::get<f32>(direct[0])));
}

TEST_CASE("Bezier distances are bounded by the finite curve endpoints") {
    // Collinear controls describe finite segments, not their extensions.
    for (const Vec2 p : {Vec2{3.f, 0.f}, Vec2{-1.f, 0.f}}) {
        REQUIRE(evalBezier(p, {{0.f, 0.f}, {1.f, 0.f}, {2.f, 0.f}}) == 1.f);
    }
    REQUIRE(evalBezier({4.f, 0.f}, {{0.f, 0.f}, {1.f, 0.f}, {2.f, 0.f}, {3.f, 0.f}}) == 1.f);
    REQUIRE(evalBezier({-1.f, 0.f}, {{0.f, 0.f}, {1.f, 0.f}, {2.f, 0.f}, {3.f, 0.f}}) == 1.f);
    REQUIRE(evalBezier({1.f, 2.f}, {{0.f, 0.f}, {1.f, 2.f}, {2.f, 0.f}}) == 1.f);
    REQUIRE(evalBezier({1.5f, 3.f}, {{0.f, 0.f}, {0.f, 2.f}, {3.f, 2.f}, {3.f, 0.f}}) == 1.5f);
}

TEST_CASE("SDF bit tables cover circle rectangle rounded rectangle line and fill") {
    REQUIRE(bitsOf(evalProgF32(Prog{}.F(3.f).F(4.f).emit(0x5B)
        .F(0.f).F(0.f).emit(0x5B).F(3.f).emit(0x7A))) == 0x40000000u);
    REQUIRE(bitsOf(evalProgF32(Prog{}.F(5.f).F(5.f).emit(0x5B)
        .F(0.f).F(0.f).emit(0x5B).F(2.f).F(3.f).emit(0x5B).emit(0x7B))) == 0x4066C15Au);
    REQUIRE(bitsOf(evalProgF32(Prog{}.F(0.f).F(0.f).emit(0x5B)
        .F(0.f).F(0.f).emit(0x5B).F(2.f).F(3.f).emit(0x5B).F(1.f).emit(0x7C))) == 0xC0000000u);
    REQUIRE(bitsOf(evalProgF32(Prog{}.F(5.f).F(5.f).emit(0x5B)
        .F(0.f).F(0.f).emit(0x5B).F(3.f).F(4.f).emit(0x5B).F(1.f).emit(0x7C))) == 0x4026C15Au);
    REQUIRE(bitsOf(evalProgF32(Prog{}.F(3.f).F(4.f).emit(0x5B)
        .F(0.f).F(0.f).emit(0x5B).F(2.f).F(0.f).emit(0x5B).emit(0x7D))) == 0x4083F07Bu);
    REQUIRE(bitsOf(evalProgF32(Prog{}.F(-0.5f).F(2.f).emit(0x7F))) == 0x3F580000u);
}

TEST_CASE("Bezier distances retain valid candidates when Newton cannot advance") {
    REQUIRE(evalBezier({3.f, 4.f}, {{0.f, 0.f}, {0.f, 0.f}, {0.f, 0.f}}) == 5.f);
    REQUIRE(evalBezier({0.f, 0.f}, {{0.f, 0.f}, {0.f, 0.f}, {0.f, 0.f}}) == 0.f);
    REQUIRE(evalBezier({3.f, 4.f}, {{0.f, 0.f}, {0.f, 0.f}, {0.f, 0.f}, {0.f, 0.f}}) == 5.f);
    REQUIRE(evalBezier({0.f, 0.f}, {{0.f, 0.f}, {0.f, 0.f}, {0.f, 0.f}, {0.f, 0.f}}) == 0.f);
    // At t=0.5 on this nonconstant curve, f and f' are both zero.
    REQUIRE(evalBezier({1.f, -0.5f}, {{0.f, 0.f}, {1.f, 1.f}, {2.f, 0.f}}) == 1.f);
    // A zero tangent at a repeated endpoint must not lose its exact zero.
    REQUIRE(evalBezier({0.f, 0.f}, {{0.f, 0.f}, {0.f, 0.f}, {2.f, 0.f}}) == 0.f);
    REQUIRE(evalBezier({0.f, 0.f}, {{0.f, 0.f}, {0.f, 0.f}, {2.f, 0.f}, {2.f, 0.f}}) == 0.f);
}

TEST_CASE("Bezier bit tables pin separately rounded Newton dot products") {
    // The first fixture distinguishes two separately rounded dots from a
    // left-associated sum of all four products (which gives 0x3f17c01c).
    REQUIRE(bitsOf(evalBezier({-2.625f, 3.625f},
                             {{-4.f, 4.75f}, {-1.f, 0.875f}, {-5.875f, -1.5f}})) == 0x3F17C01Bu);
    REQUIRE(bitsOf(evalBezier({-1.625f, -3.375f},
                             {{-5.f, -1.75f}, {-5.875f, 3.875f}, {3.5f, 3.375f}})) == 0x406FBBB2u);
    REQUIRE(bitsOf(evalBezier({1.625f, 3.375f},
                             {{-1.f, -1.75f}, {-2.875f, 3.875f}, {3.5f, 3.375f}, {4.f, -2.f}})) ==
            0x3FA79662u);
}

TEST_CASE("Bezier nonfinite coordinates return the canonical NaN") {
    for (const f32 value : {std::numeric_limits<f32>::infinity(),
                           -std::numeric_limits<f32>::infinity(), f32FromBits(0x7FC01234u)}) {
        REQUIRE(bitsOf(evalBezier({value, 0.f}, {{0.f, 0.f}, {1.f, 1.f}, {2.f, 0.f}})) ==
                0x7FC00000u);
        REQUIRE(bitsOf(evalBezier({0.f, 0.f}, {{0.f, 0.f}, {value, 1.f}, {2.f, 0.f}})) ==
                0x7FC00000u);
        REQUIRE(bitsOf(evalBezier({0.f, 0.f},
                                 {{0.f, 0.f}, {1.f, 1.f}, {2.f, 0.f}, {value, 0.f}})) == 0x7FC00000u);
    }
}
