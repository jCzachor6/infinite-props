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

TEST_CASE("gain and quintic match the pinned spec vectors bit for bit") {
    REQUIRE(bitsOf(evalProgF32(Prog{}.F(0.25f).F(0.5f).emit(0x8F))) ==
            0x3E800000u); // the identity at k = 0.5
    REQUIRE(bitsOf(evalProgF32(Prog{}.F(0.5f).F(0.3f).emit(0x8F))) ==
            0x3F000000u); // the fixed point
    REQUIRE(bitsOf(evalProgF32(Prog{}.F(0.25f).F(0.1f).emit(0x8F))) ==
            0x3D4CCCCDu);
    REQUIRE(bitsOf(evalProgF32(Prog{}.F(0.75f).F(0.9f).emit(0x8F))) ==
            0x3F0CCCCDu);
    REQUIRE(bitsOf(evalProgF32(Prog{}.F(-1.f).F(0.5f).emit(0x8F))) ==
            0x00000000u); // the t clamp
    REQUIRE(bitsOf(evalProgF32(Prog{}.F(0.5f).F(0.f).emit(0x8F))) ==
            0x3F000000u); // the k clamp keeps the fixed point
    REQUIRE(bitsOf(evalProgF32(Prog{}.F(0.f).emit(0x99))) == 0x00000000u);
    REQUIRE(bitsOf(evalProgF32(Prog{}.F(1.f).emit(0x99))) == 0x3F800000u);
    REQUIRE(bitsOf(evalProgF32(Prog{}.F(0.5f).emit(0x99))) == 0x3F000000u);
    REQUIRE(bitsOf(evalProgF32(Prog{}.F(0.25f).emit(0x99))) == 0x3DD40000u);
    REQUIRE(bitsOf(evalProgF32(Prog{}.F(-2.f).emit(0x99))) == 0x00000000u);
    // The monotonicity: the gain pushes towards the extremes as k drops.
    REQUIRE(evalProgF32(Prog{}.F(0.25f).F(0.1f).emit(0x8F)) <
            evalProgF32(Prog{}.F(0.25f).F(0.5f).emit(0x8F)));
    REQUIRE(evalProgF32(Prog{}.F(0.75f).F(0.1f).emit(0x8F)) >
            evalProgF32(Prog{}.F(0.75f).F(0.5f).emit(0x8F)));
}

TEST_CASE("gain defines the singular parameter limit and keeps finite inputs in range") {
    const f32 inf = std::numeric_limits<f32>::infinity();
    const std::pair<f32, u32> limits[] = {
        {-inf, 0x00000000u}, {-1.f, 0x00000000u}, {-0.f, 0x00000000u},
        {0.f, 0x00000000u}, {std::numeric_limits<f32>::denorm_min(), 0x3F000000u},
        {1e-9f, 0x3F000000u}, {0.25f, 0x3F000000u}, {0.5f, 0x3F000000u},
        {0.75f, 0x3F000000u}, {std::nextafter(1.f, 0.f), 0x3F000000u},
        {1.f, 0x3F800000u}, {2.f, 0x3F800000u}, {inf, 0x3F800000u},
    };
    for (const f32 k : {1.f, 2.f, inf}) {
        for (const auto& [t, expected] : limits) {
            CAPTURE(t, k);
            REQUIRE(bitsOf(evalProgF32(Prog{}.F(t).F(k).emit(0x8F))) == expected);
        }
    }
    for (const f32 k : {0.f, 1e-4f, 0.01f, 0.5f, 0.9f,
                        std::nextafter(1.f, 0.f), 1.f, 2.f}) {
        for (unsigned i = 0; i <= 128; ++i) {
            const f32 t = static_cast<f32>(i) / 128.f;
            const f32 value = evalProgF32(Prog{}.F(t).F(k).emit(0x8F));
            CAPTURE(t, k);
            REQUIRE(std::isfinite(value));
            REQUIRE((value >= 0.f && value <= 1.f));
        }
    }
    for (const f32 nan : {f32FromBits(0x7FC01234u), f32FromBits(0xFFC01234u)}) {
        for (const f32 other : {0.f, 0.5f, 1.f}) {
            REQUIRE(bitsOf(evalProgF32(Prog{}.F(nan).F(other).emit(0x8F))) == 0x7FC00000u);
            REQUIRE(bitsOf(evalProgF32(Prog{}.F(other).F(nan).emit(0x8F))) == 0x7FC00000u);
        }
    }
}

TEST_CASE("gain endpoints can feed colour interpolation without nonfinite channels") {
    const Color black{0, 0, 0, 255}, white{255, 255, 255, 255};
    REQUIRE(evalProgColor(Prog{}.C(black).C(white).F(0.f).F(1.f).emit(0x8F).emit(0x50)) == black);
    REQUIRE(evalProgColor(Prog{}.C(black).C(white).F(1.f).F(1.f).emit(0x8F).emit(0x50)) == white);
    REQUIRE(evalProgColor(Prog{}.C(black).C(white).F(1e-9f).F(1.f).emit(0x8F).emit(0x50)) ==
            Color{127, 127, 127, 255});
}

TEST_CASE("the curve toolkit matches the pinned formulas") {
    // smoothstep: the two-sided smootherstep.
    REQUIRE(evalProgF32(Prog{}.F(0.f).F(1.f).F(0.5f).emit(0xA1)) == 0.5f);
    REQUIRE(evalProgF32(Prog{}.F(0.f).F(1.f).F(0.25f).emit(0xA1)) == 0.15625f);
    REQUIRE(evalProgF32(Prog{}.F(2.f).F(4.f).F(3.f).emit(0xA1)) == 0.5f);
    REQUIRE(evalProgF32(Prog{}.F(0.f).F(1.f).F(-1.f).emit(0xA1)) == 0.f);
    REQUIRE(evalProgF32(Prog{}.F(0.f).F(1.f).F(2.f).emit(0xA1)) == 1.f);
    // The zero-denominator guard.
    REQUIRE(evalProgF32(Prog{}.F(1.f).F(1.f).F(0.5f).emit(0xA1)) == 0.f);
    // remap: the linear rescale with the clamp.
    REQUIRE(evalProgF32(Prog{}.F(0.5f).F(0.f).F(1.f).F(10.f).F(20.f).emit(0xA2)) == 15.f);
    REQUIRE(evalProgF32(Prog{}.F(2.f).F(0.f).F(1.f).F(10.f).F(20.f).emit(0xA2)) == 20.f);
    REQUIRE(evalProgF32(Prog{}.F(-1.f).F(0.f).F(1.f).F(10.f).F(20.f).emit(0xA2)) == 10.f);
    // clamp: the bounds in either order.
    REQUIRE(evalProgF32(Prog{}.F(1.5f).F(0.f).F(1.f).emit(0xA3)) == 1.f);
    REQUIRE(evalProgF32(Prog{}.F(-1.f).F(0.f).F(1.f).emit(0xA3)) == 0.f);
    REQUIRE(evalProgF32(Prog{}.F(0.5f).F(1.f).F(0.f).emit(0xA3)) == 0.5f);
    // The self-consistency: smoothstep(a,b,t) == smooth((t-a)/(b-a)) for
    // the interior points, and clamp == min(max(t,a),b) for the ordered
    // bounds.
    REQUIRE(bitsOf(evalProgF32(Prog{}.F(0.f).F(1.f).F(0.375f).emit(0xA1))) ==
            bitsOf(evalProgF32(Prog{}.F(0.375f).emit(0x72))));
}

TEST_CASE("curve normalization uses clamp's minNum and signed-zero semantics") {
    for (const u32 bits : {0x7FC00000u, 0xFFC00000u, 0x7FC01234u, 0xFFC01234u}) {
        const f32 nan = f32FromBits(bits);
        CAPTURE(bits);
        REQUIRE(bitsOf(evalProgF32(Prog{}.F(0.f).F(1.f).F(nan).emit(0xA1))) == 0);
        REQUIRE(evalProgF32(Prog{}.F(nan).F(0.f).F(1.f).F(10.f).F(20.f).emit(0xA2)) == 10.f);
        REQUIRE(bitsOf(evalProgF32(Prog{}.F(nan).F(1.f).F(0.5f).emit(0xA1))) == 0);
        REQUIRE(bitsOf(evalProgF32(Prog{}.F(0.f).F(nan).F(0.5f).emit(0xA1))) == 0);
        REQUIRE(evalProgF32(Prog{}.F(0.5f).F(nan).F(1.f).F(10.f).F(20.f).emit(0xA2)) == 10.f);
        REQUIRE(evalProgF32(Prog{}.F(0.5f).F(0.f).F(nan).F(10.f).F(20.f).emit(0xA2)) == 10.f);
    }
    REQUIRE(bitsOf(evalProgF32(Prog{}.F(-0.f).F(0.f).F(1.f).F(-0.f).F(-1.f).emit(0xA2))) ==
            0x80000000u);
    const f32 inf = std::numeric_limits<f32>::infinity();
    REQUIRE(evalProgF32(Prog{}.F(0.f).F(1.f).F(inf).emit(0xA1)) == 1.f);
    REQUIRE(evalProgF32(Prog{}.F(0.f).F(1.f).F(-inf).emit(0xA1)) == 0.f);
}

TEST_CASE("clamp uses pinned minNum and maxNum for NaN payloads and zero bounds") {
    for (const u32 tBits : {0x7FC00000u, 0xFFC00000u, 0x7FC01234u, 0xFFC01234u}) {
        const f32 t = f32FromBits(tBits);
        for (const u32 aBits : {0x7FC00000u, 0xFFC00000u, 0x7FC01234u, 0xFFC01234u}) {
            for (const u32 bBits : {0x7FC00000u, 0xFFC00000u, 0x7FC01234u, 0xFFC01234u}) {
                CAPTURE(tBits, aBits, bBits);
                REQUIRE(bitsOf(evalProgF32(Prog{}.F(t).F(f32FromBits(aBits))
                                              .F(f32FromBits(bBits)).emit(0xA3))) == 0x7FC00000u);
            }
        }
        REQUIRE(evalProgF32(Prog{}.F(t).F(0.f).F(1.f).emit(0xA3)) == 0.f);
        REQUIRE(evalProgF32(Prog{}.F(2.f).F(t).F(1.f).emit(0xA3)) == 1.f);
        REQUIRE(evalProgF32(Prog{}.F(2.f).F(t).F(t).emit(0xA3)) == 2.f);
    }
    REQUIRE(bitsOf(evalProgF32(Prog{}.F(0.f).F(-0.f).F(-0.f).emit(0xA3))) == 0x80000000u);
    REQUIRE(bitsOf(evalProgF32(Prog{}.F(-0.f).F(0.f).F(0.f).emit(0xA3))) == 0);
    REQUIRE(bitsOf(evalProgF32(Prog{}.F(1.f).F(0.f).F(-0.f).emit(0xA3))) == 0);
    REQUIRE(bitsOf(evalProgF32(Prog{}.F(-1.f).F(0.f).F(-0.f).emit(0xA3))) == 0x80000000u);
}
