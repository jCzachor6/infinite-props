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

TEST_CASE("vec2 make/add/sub are component-wise IEEE") {
    const auto stack = evalProg(Prog{}.F(1.5f).F(2.5f).emit(0x5B));
    REQUIRE(stack.size() == 1);
    const auto v = std::get<Vec2>(stack[0]);
    REQUIRE(v.x == 1.5f);
    REQUIRE(v.y == 2.5f);

    const auto add = std::get<Vec2>(
        evalProg(Prog{}.F(1.f).F(2.f).emit(0x5B).F(0.5f).F(-1.f).emit(0x5B).emit(0x5C))[0]);
    REQUIRE(add.x == 1.5f);
    REQUIRE(add.y == 1.f);

    const auto sub = std::get<Vec2>(
        evalProg(Prog{}.F(1.f).F(2.f).emit(0x5B).F(0.5f).F(-1.f).emit(0x5B).emit(0x5D))[0]);
    REQUIRE(sub.x == 0.5f);
    REQUIRE(sub.y == 3.f);
}

TEST_CASE("vec2 mul/div accept scalars and vectors") {
    // Scalar: {1, 2} * 3 = {3, 6}.
    const auto scaled = std::get<Vec2>(
        evalProg(Prog{}.F(1.f).F(2.f).emit(0x5B).F(3.f).emit(0x5E))[0]);
    REQUIRE(scaled.x == 3.f);
    REQUIRE(scaled.y == 6.f);
    // Vector (Hadamard): {1, 2} * {3, 4} = {3, 8}.
    const auto hadamard = std::get<Vec2>(
        evalProg(Prog{}.F(1.f).F(2.f).emit(0x5B).F(3.f).F(4.f).emit(0x5B).emit(0x5E))[0]);
    REQUIRE(hadamard.x == 3.f);
    REQUIRE(hadamard.y == 8.f);
    // Scalar division: {3, 6} / 2 = {1.5, 3}.
    const auto divided = std::get<Vec2>(
        evalProg(Prog{}.F(3.f).F(6.f).emit(0x5B).F(2.f).emit(0x5F))[0]);
    REQUIRE(divided.x == 1.5f);
    REQUIRE(divided.y == 3.f);
    // Vector division: {6, 9} / {2, 3} = {3, 3}.
    const auto vdivided = std::get<Vec2>(
        evalProg(Prog{}.F(6.f).F(9.f).emit(0x5B).F(2.f).F(3.f).emit(0x5B).emit(0x5F))[0]);
    REQUIRE(vdivided.x == 3.f);
    REQUIRE(vdivided.y == 3.f);
}

TEST_CASE("vec2 neg/len/dot follow the pinned formulas") {
    const auto neg = std::get<Vec2>(evalProg(Prog{}.F(1.f).F(-2.f).emit(0x5B).emit(0x74))[0]);
    REQUIRE(neg.x == -1.f);
    REQUIRE(neg.y == 2.f);

    // len({3, 4}) = 5 exactly; len({-3, -4}) = 5.
    REQUIRE(evalProgF32(Prog{}.F(3.f).F(4.f).emit(0x5B).emit(0x75)) == 5.f);
    REQUIRE(evalProgF32(Prog{}.F(-3.f).F(-4.f).emit(0x5B).emit(0x75)) == 5.f);

    // dot({2, 3}, {4, 5}) = 23.
    REQUIRE(evalProgF32(
                Prog{}.F(2.f).F(3.f).emit(0x5B).F(4.f).F(5.f).emit(0x5B).emit(0x76)) ==
            23.f);
}

TEST_CASE("vec2 rot uses the pinned sin/cos — self-consistent with the opcodes") {
    // rot({1, 0}, a) must equal {cos(a), -sin(a)} from the same pinned
    // algorithms the f32.cos/f32.sin opcodes use (spec §12.7).
    const f32 angle = 1.2345f;
    const auto rotated = std::get<Vec2>(
        evalProg(Prog{}.F(1.f).F(0.f).emit(0x5B).F(angle).emit(0x77))[0]);
    REQUIRE(rotated.x == evalProgF32(Prog{}.F(angle).emit(0x53))); // f32.cos
    REQUIRE(rotated.y == -evalProgF32(Prog{}.F(angle).emit(0x4F))); // -f32.sin
    // A quarter turn of {1, 0} lands near {0, -1} (the pinned cos(pi/2)
    // is tiny but not exactly zero).
    const f32 piOver2 = 1.57079632679f;
    const auto quarter = std::get<Vec2>(
        evalProg(Prog{}.F(1.f).F(0.f).emit(0x5B).F(piOver2).emit(0x77))[0]);
    REQUIRE(std::fabs(quarter.x) < 1e-5f);
    REQUIRE(quarter.y == -evalProgF32(Prog{}.F(piOver2).emit(0x4F)));
    // And the rotation of {0, 1} by pi/2 lands on {1, 0}-ish: the
    // counterclockwise sense.
    const auto quarter2 = std::get<Vec2>(
        evalProg(Prog{}.F(0.f).F(1.f).emit(0x5B).F(piOver2).emit(0x77))[0]);
    REQUIRE(quarter2.x == -quarter.y);
    REQUIRE(quarter2.y == quarter.x);
}

TEST_CASE("vec2 x/y extract the components") {
    REQUIRE(evalProgF32(Prog{}.F(1.5f).F(2.5f).emit(0x5B).emit(0x78)) == 1.5f);
    REQUIRE(evalProgF32(Prog{}.F(1.5f).F(2.5f).emit(0x5B).emit(0x79)) == 2.5f);
}

TEST_CASE("fold and polar follow the pinned identities") {
    // fold(v, 2) mirrors into the right half-plane.
    const auto f1 = std::get<Vec2>(
        evalProg(Prog{}.F(1.f).F(0.f).emit(0x5B).I(2).emit(0x9E))[0]);
    REQUIRE(f1.x == 1.f);
    REQUIRE(f1.y == 0.f);
    const auto f2 = std::get<Vec2>(
        evalProg(Prog{}.F(-1.f).F(0.f).emit(0x5B).I(2).emit(0x9E))[0]);
    REQUIRE(f2.x == 1.f);
    REQUIRE(f2.y == 0.f);
    const auto f3 = std::get<Vec2>(
        evalProg(Prog{}.F(0.f).F(-1.f).emit(0x5B).I(2).emit(0x9E))[0]);
    REQUIRE(std::fabs(f3.x) < 1e-5f); // the pinned cos(pi/2), not exactly 0
    REQUIRE(f3.y == 1.f);
    // The 4-fold fold keeps the first quadrant points; the length is
    // preserved.
    const auto f4 = std::get<Vec2>(
        evalProg(Prog{}.F(3.f).F(4.f).emit(0x5B).I(4).emit(0x9E))[0]);
    REQUIRE(f4.x >= 0.f);
    REQUIRE(f4.y >= 0.f);
    REQUIRE(evalProgF32(Prog{}.F(f4.x).F(f4.y).emit(0x5B).emit(0x75)) == 5.f);
    // The fold count clamps to [1, 64].
    const auto f5 = std::get<Vec2>(
        evalProg(Prog{}.F(1.f).F(0.f).emit(0x5B).I(0).emit(0x9E))[0]);
    REQUIRE(f5.x == 1.f);
    REQUIRE(f5.y == 0.f);

    // polar({3,4}) = {5, atan2(-4,3)} in the downward-y asset plane.
    const auto p = std::get<Vec2>(
        evalProg(Prog{}.F(3.f).F(4.f).emit(0x5B).emit(0x9F))[0]);
    REQUIRE(p.x == 5.f);
    REQUIRE(bitsOf(p.y) == bitsOf(evalProgF32(Prog{}.F(-4.f).F(3.f).emit(0x5A))));
}

TEST_CASE("fold canonicalizes every NaN component while retaining infinity and finite results") {
    const auto fold = [](Vec2 v, i32 n) {
        return std::get<Vec2>(
            evalProg(Prog{}.F(v.x).F(v.y).emit(0x5B).I(n).emit(0x9E))[0]);
    };
    for (const u32 nanBits : {0x7FC00000u, 0xFFC00000u, 0x7FC01234u, 0xFFC01234u}) {
        const f32 nan = f32FromBits(nanBits);
        for (const Vec2 v : {Vec2{nan, 0.f}, Vec2{0.f, nan}, Vec2{nan, 1.f},
                             Vec2{1.f, nan}, Vec2{nan, -nan}}) {
            for (const i32 n : {0, 1, 2, 4, 64, 65}) {
                CAPTURE(nanBits, n);
                const Vec2 result = fold(v, n);
                REQUIRE(bitsOf(result.x) == 0x7FC00000u);
                REQUIRE(bitsOf(result.y) == 0x7FC00000u);
            }
        }
    }
    // A NaN can also arise in the final product inf * 0, including when
    // a finite vector overflows the binary32 radius computation.
    const f32 inf = std::numeric_limits<f32>::infinity();
    for (const Vec2 v : {Vec2{inf, 0.f}, Vec2{-inf, 0.f}, Vec2{0.f, inf},
                         Vec2{0.f, -inf}, Vec2{1e20f, 0.f}, Vec2{0.f, 1e20f}}) {
        const Vec2 result = fold(v, 4);
        REQUIRE(bitsOf(result.x) == 0x7F800000u);
        REQUIRE(bitsOf(result.y) == 0x7FC00000u);
    }
    const Vec2 diagonal = fold(Vec2{inf, inf}, 4);
    REQUIRE(bitsOf(diagonal.x) == 0x7F800000u);
    REQUIRE(bitsOf(diagonal.y) == 0x7F800000u);
    const Vec2 identity = fold(Vec2{1.f, 0.f}, 4);
    REQUIRE(bitsOf(identity.x) == 0x3F800000u);
    REQUIRE(bitsOf(identity.y) == 0x00000000u);
}
