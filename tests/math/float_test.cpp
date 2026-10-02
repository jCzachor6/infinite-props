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

TEST_CASE("f32 arithmetic is IEEE binary32") {
    REQUIRE(evalProgF32(Prog{}.F(1.5f).F(2.25f).emit(0x40)) == 3.75f);
    REQUIRE(evalProgF32(Prog{}.F(5.f).F(2.f).emit(0x41)) == 3.f);
    REQUIRE(evalProgF32(Prog{}.F(2.5f).F(4.f).emit(0x42)) == 10.f);
    REQUIRE(evalProgF32(Prog{}.F(10.f).F(4.f).emit(0x43)) == 2.5f);
    REQUIRE(evalProgF32(Prog{}.F(-1.5f).emit(0x4A)) == 1.5f);
    REQUIRE(evalProgF32(Prog{}.F(-2.5f).emit(0x4B)) == 2.5f);
    REQUIRE(evalProgF32(Prog{}.F(16.f).emit(0x4C)) == 4.f);

    // IEEE edge cases: division by zero yields infinities, no trap.
    const f32 inf = std::numeric_limits<f32>::infinity();
    REQUIRE(evalProgF32(Prog{}.F(1.f).F(0.f).emit(0x43)) == inf);
    REQUIRE(evalProgF32(Prog{}.F(-1.f).F(0.f).emit(0x43)) == -inf);
    REQUIRE(std::isnan(evalProgF32(Prog{}.F(-1.f).emit(0x4C)))); // sqrt(-1)
}

TEST_CASE("f32 multiplication is not contracted into an FMA") {
    // (1 + 2^-23)^2 - (1 + 2^-22): the exact result is 2^-46. Fused, the
    // product keeps its low bit and the subtraction yields 2^-46; unfused,
    // the product rounds to 1 + 2^-22 and the subtraction yields 0.
    const f32 a = 1.0f + 1.0f / 8388608.0f;  // 1 + 2^-23
    const f32 b = a;
    const f32 c = -(1.0f + 1.0f / 4194304.0f); // -(1 + 2^-22)
    const f32 unfused = (a * b) + c;
    const f32 fused = std::fma(a, b, c);
    // The vector must actually be contraction-sensitive, or this test is
    // vacuous.
    REQUIRE(bitsOf(unfused) != bitsOf(fused));
    REQUIRE(unfused == 0.f);
    REQUIRE(bitsOf(fused) == 0x28800000u); // 2^-46 in binary32

    const auto result = evalProgF32(Prog{}.F(a).F(b).emit(0x42).F(c).emit(0x40));
    REQUIRE(bitsOf(result) == bitsOf(unfused));
}

TEST_CASE("f32 comparisons follow IEEE predicates") {
    REQUIRE(evalProgI32(Prog{}.F(1.f).F(1.f).emit(0x44)) == 1);
    REQUIRE(evalProgI32(Prog{}.F(1.f).F(2.f).emit(0x45)) == 1);
    REQUIRE(evalProgI32(Prog{}.F(1.f).F(2.f).emit(0x46)) == 1);
    REQUIRE(evalProgI32(Prog{}.F(2.f).F(1.f).emit(0x47)) == 1);
    REQUIRE(evalProgI32(Prog{}.F(1.f).F(1.f).emit(0x48)) == 1);
    REQUIRE(evalProgI32(Prog{}.F(1.f).F(1.f).emit(0x49)) == 1);

    const f32 nan = std::numeric_limits<f32>::quiet_NaN();
    REQUIRE(evalProgI32(Prog{}.F(nan).F(nan).emit(0x44)) == 0); // NaN != NaN via eq
    REQUIRE(evalProgI32(Prog{}.F(nan).F(nan).emit(0x45)) == 1); // ne is true for NaN
    REQUIRE(evalProgI32(Prog{}.F(nan).F(1.f).emit(0x46)) == 0); // ordering false
}

TEST_CASE("the math package matches the pinned algorithm bit for bit") {
    // Spec §12.8: floor/ceil are the IEEE operations; exp/pow/atan2 are the
    // pinned double-precision polynomials. The bit patterns below are the
    // frozen outputs — regenerate only with a spec update.
    const f32 nan = std::numeric_limits<f32>::quiet_NaN();

    struct Unary {
        u32 input;
        u32 expected;
    };
    const Unary expCases[] = {
        {0x00000000u, 0x3F800000u}, // exp(0)
        {0x3F800000u, 0x402DF854u}, // exp(1)
        {0xBF800000u, 0x3EBC5AB2u}, // exp(-1)
        {0x3F000000u, 0x3FD3094Cu}, // exp(0.5)
        {0x40000000u, 0x40EC7326u}, // exp(2)
        {0x3F317218u, 0x40000000u}, // exp(ln2) = 2 exactly
        {0x40C90FDBu, 0x4405DF79u}, // exp(pi)
        {0x42C80000u, 0x7F800000u}, // exp(100) overflows f32
        {0x43000000u, 0x7F800000u}, // exp(128)
        {0x42FE0000u, 0x7F800000u}, // exp(127)
        {0x7F800000u, 0x7F800000u}, // exp(+inf)
        {0xFF800000u, 0x00000000u}, // exp(-inf)
        {0x7FC00000u, 0x7FC00000u}, // exp(NaN)
    };
    for (const Unary& testCase : expCases) {
        INFO("exp input 0x" << std::hex << testCase.input);
        REQUIRE(bitsOf(evalProgF32(Prog{}.F(f32FromBits(testCase.input)).emit(0x59))) ==
                testCase.expected);
    }

    struct Binary {
        u32 a;
        u32 b;
        u32 expected;
    };
    const Binary powCases[] = {
        {0x40000000u, 0x41200000u, 0x44800000u}, // 2^10
        {0x40000000u, 0xC0000000u, 0x3E800000u}, // 2^-2
        {0x40000000u, 0x3F000000u, 0x3FB504F3u}, // 2^0.5 = sqrt(2)
        {0x3F000000u, 0x40400000u, 0x3E000000u}, // 0.5^3
        {0xC0000000u, 0x40400000u, 0xC1000000u}, // (-2)^3
        {0xC0000000u, 0x40000000u, 0x40800000u}, // (-2)^2
        {0xC0000000u, 0x3F000000u, 0x7FC00000u}, // (-2)^0.5 -> NaN
        {0x00000000u, 0x40400000u, 0x00000000u}, // 0^3
        {0x00000000u, 0xC0400000u, 0x7F800000u}, // 0^-3
        {0x80000000u, 0x40400000u, 0x80000000u}, // (-0)^3 = -0
        {0x80000000u, 0x40000000u, 0x00000000u}, // (-0)^2 = +0
        {0x3F800000u, 0x447A0000u, 0x3F800000u}, // 1^1000
        {0x7F800000u, 0x40000000u, 0x7F800000u}, // inf^2
        {0x7F800000u, 0xC0000000u, 0x00000000u}, // inf^-2
        {0xFF800000u, 0x40400000u, 0xFF800000u}, // (-inf)^3
        {0xFF800000u, 0x40000000u, 0x7F800000u}, // (-inf)^2
        {0x40490FDBu, 0x40000000u, 0x411DE9E7u}, // pi^2
        {0x3F317218u, 0x40000000u, 0x3EF5FDF0u}, // ln2^2
        {0x40A00000u, 0x00000000u, 0x3F800000u}, // 5^0 = 1
        {0x7FC00000u, 0x00000000u, 0x3F800000u}, // NaN^0 = 1
        {0xBF800000u, 0x7F800000u, 0x3F800000u}, // (-1)^+inf
        {0xBF800000u, 0xFF800000u, 0x3F800000u}, // (-1)^-inf
        {0xBF000000u, 0x7F800000u, 0x00000000u}, // (-0.5)^+inf
        {0xBF000000u, 0xFF800000u, 0x7F800000u}, // (-0.5)^-inf
        {0xC0000000u, 0x7F800000u, 0x7F800000u}, // (-2)^+inf
        {0xC0000000u, 0xFF800000u, 0x00000000u}, // (-2)^-inf
        {0x80000000u, 0x7F800000u, 0x00000000u}, // (-0)^+inf
        {0x80000000u, 0xFF800000u, 0x7F800000u}, // (-0)^-inf
        {0xFF800000u, 0x7F800000u, 0x7F800000u}, // (-inf)^+inf
        {0xFF800000u, 0xFF800000u, 0x00000000u}, // (-inf)^-inf
    };
    for (const Binary& testCase : powCases) {
        INFO("pow 0x" << std::hex << testCase.a << " ^ 0x" << testCase.b);
        REQUIRE(bitsOf(evalProgF32(Prog{}.F(f32FromBits(testCase.a))
                                           .F(f32FromBits(testCase.b))
                                           .emit(0x58))) == testCase.expected);
    }

    const Binary atan2Cases[] = {
        {0x3F800000u, 0x3F800000u, 0x3F490FDBu}, // (1,1) pi/4
        {0x3F800000u, 0xBF800000u, 0x4016CBE4u}, // (1,-1) 3pi/4
        {0xBF800000u, 0x3F800000u, 0xBF490FDBu}, // (-1,1) -pi/4
        {0xBF800000u, 0xBF800000u, 0xC016CBE4u}, // (-1,-1) -3pi/4
        {0x00000000u, 0x3F800000u, 0x00000000u}, // (0,1)
        {0x00000000u, 0xBF800000u, 0x40490FDBu}, // (0,-1) pi
        {0x80000000u, 0xBF800000u, 0xC0490FDBu}, // (-0,-1) -pi
        {0x3F800000u, 0x00000000u, 0x3FC90FDBu}, // (1,0) pi/2
        {0xBF800000u, 0x00000000u, 0xBFC90FDBu}, // (-1,0) -pi/2
        {0x3F000000u, 0x3F000000u, 0x3F490FDBu}, // (0.5,0.5)
        {0x3F000000u, 0xBF000000u, 0x4016CBE4u}, // (0.5,-0.5)
        {0x40490FDBu, 0x40490FDBu, 0x3F490FDBu}, // (pi,pi)
        {0x3F800000u, 0x7F800000u, 0x00000000u}, // (1,inf)
        {0x7F800000u, 0x3F800000u, 0x3FC90FDBu}, // (inf,1)
        {0x7F800000u, 0x7F800000u, 0x3F490FDBu}, // (inf,inf)
        {0x7F800000u, 0xFF800000u, 0x4016CBE4u}, // (inf,-inf)
        {0xFF800000u, 0x7F800000u, 0xBF490FDBu}, // (-inf,inf)
        {0xFF800000u, 0x3F800000u, 0xBFC90FDBu}, // (-inf,1)
        {0x3F800000u, 0xFF800000u, 0x40490FDBu}, // (1,-inf)
        {0xBF800000u, 0xFF800000u, 0xC0490FDBu}, // (-1,-inf)
        {0x00000000u, 0x00000000u, 0x00000000u}, // (0,0)
        {0x80000000u, 0x80000000u, 0x80000000u}, // (-0,-0)
    };
    for (const Binary& testCase : atan2Cases) {
        INFO("atan2 0x" << std::hex << testCase.a << ", 0x" << testCase.b);
        REQUIRE(bitsOf(evalProgF32(Prog{}.F(f32FromBits(testCase.a))
                                           .F(f32FromBits(testCase.b))
                                           .emit(0x5A))) == testCase.expected);
    }

    // floor/ceil are the IEEE operations.
    const Unary floorCases[] = {
        {0x3FC00000u, 0x3F800000u}, // floor(1.5)
        {0xBFC00000u, 0xC0000000u}, // floor(-1.5)
        {0x40000000u, 0x40000000u}, // floor(2)
        {0x7FC00000u, 0x7FC00000u}, // floor(NaN)
        {0x7F800000u, 0x7F800000u}, // floor(+inf)
    };
    for (const Unary& testCase : floorCases) {
        REQUIRE(bitsOf(evalProgF32(Prog{}.F(f32FromBits(testCase.input)).emit(0x54))) ==
                testCase.expected);
    }
    const Unary ceilCases[] = {
        {0x3FC00000u, 0x40000000u}, // ceil(1.5)
        {0xBFC00000u, 0xBF800000u}, // ceil(-1.5)
    };
    for (const Unary& testCase : ceilCases) {
        REQUIRE(bitsOf(evalProgF32(Prog{}.F(f32FromBits(testCase.input)).emit(0x55))) ==
                testCase.expected);
    }

    // min/max: minNum/maxNum semantics (NaN ignored unless both are NaN).
    REQUIRE(std::isnan(evalProgF32(Prog{}.F(nan).F(1.f).emit(0x56))) == false);
    REQUIRE(evalProgF32(Prog{}.F(nan).F(1.f).emit(0x56)) == 1.f);
    REQUIRE(evalProgF32(Prog{}.F(1.f).F(nan).emit(0x56)) == 1.f);
    REQUIRE(std::isnan(evalProgF32(Prog{}.F(nan).F(nan).emit(0x56))));
    REQUIRE(std::isnan(evalProgF32(Prog{}.F(nan).F(nan).emit(0x57))));
    // -0 < +0 for the ordering.
    REQUIRE(bitsOf(evalProgF32(Prog{}.F(-0.f).F(0.f).emit(0x56))) == 0x80000000u);
    REQUIRE(bitsOf(evalProgF32(Prog{}.F(-0.f).F(0.f).emit(0x57))) == 0x00000000u);
}

TEST_CASE("min and max canonicalize all NaNs and order signed zero explicitly") {
    const u32 nans[] = {0x7fc00000u, 0xffc00000u, 0x7fc12345u,
                        0xffc54321u, 0x7f800001u, 0xff800001u};
    for (const u8 op : {u8{0x56}, u8{0x57}}) {
        for (const u32 a : nans) {
            for (const u32 b : nans) {
                REQUIRE(bitsOf(evalProgF32(Prog{}.F(f32FromBits(a)).F(f32FromBits(b)).emit(op))) ==
                        0x7fc00000u);
            }
            for (const f32 value : {0.f, -0.f, 1.f, -1.f,
                                   std::numeric_limits<f32>::infinity()}) {
                REQUIRE(bitsOf(evalProgF32(Prog{}.F(f32FromBits(a)).F(value).emit(op))) == bitsOf(value));
                REQUIRE(bitsOf(evalProgF32(Prog{}.F(value).F(f32FromBits(a)).emit(op))) == bitsOf(value));
            }
        }
        for (const f32 a : {0.f, -0.f}) {
            for (const f32 b : {0.f, -0.f}) {
                const bool negative = op == 0x56 ? std::signbit(a) || std::signbit(b)
                                                 : std::signbit(a) && std::signbit(b);
                REQUIRE(bitsOf(evalProgF32(Prog{}.F(a).F(b).emit(op))) ==
                        (negative ? 0x80000000u : 0u));
            }
        }
    }
}

TEST_CASE("atan2 handles unequal magnitudes in every quadrant and near axes") {
    for (const f32 y : {0.125f, 1.f, 2.f, 16.f, 1e-30f, 1e30f}) {
        for (const f32 x : {0.125f, 1.f, 2.f, 16.f, 1e-30f, 1e30f}) {
            for (const f32 sy : {-1.f, 1.f}) {
                for (const f32 sx : {-1.f, 1.f}) {
                    const f32 actual = evalProgF32(Prog{}.F(y * sy).F(x * sx).emit(0x5A));
                    const f32 expected = std::atan2(y * sy, x * sx);
                    INFO("atan2(" << y * sy << ", " << x * sx << ")");
                    const u32 a = bitsOf(actual);
                    const u32 b = bitsOf(expected);
                    REQUIRE((a > b ? a - b : b - a) <= 2u);
                }
            }
        }
    }
}

TEST_CASE("the math package agrees with libm within the pinned tolerance") {
    std::set<std::pair<u32, u32>> powers;
    std::set<std::pair<u32, u32>> angles;
    bool quadrants[4] = {};
    f32 minimumBase = 4.f, maximumBase = 0.f;
    f32 minimumExponent = 4.f, maximumExponent = -4.f;
    for (u32 i = 0; i < 2000; ++i) {
        const u32 bits = i * 2654435761u + 0x9e3779b9u;
        f32 value = f32FromBits((bits & 0x7FFFFFu) | 0x3F000000u); // in [0.5, 1)
        value *= (i % 2 == 0 ? 1.f : -1.f);
        value *= (i % 17 == 0 ? 8.f : 1.f);
        const f32 gotExp = evalProgF32(Prog{}.F(value).emit(0x59));
        const f32 wantExp = std::exp(value);
        REQUIRE(ulp(gotExp, wantExp) <= 1u);

        // pow over positive bases and moderate exponents.
        const f32 base = 0.25f + unitFromBits(bits) * 3.75f;
        const u32 other = i * 2246822519u + 0x85ebca6bu;
        const f32 exponent = unitFromBits(other) * 8.f - 4.f;
        powers.emplace(bitsOf(base), bitsOf(exponent));
        minimumBase = std::min(minimumBase, base);
        maximumBase = std::max(maximumBase, base);
        minimumExponent = std::min(minimumExponent, exponent);
        maximumExponent = std::max(maximumExponent, exponent);
        const f32 gotPow = evalProgF32(Prog{}.F(base).F(exponent).emit(0x58));
        const f32 wantPow = std::pow(base, exponent);
        REQUIRE(ulp(gotPow, wantPow) <= 2u);

        // atan2 over the full circle.
        const f32 y = unitFromBits(bits) * 8.f - 4.f;
        const f32 x = unitFromBits(other) * 8.f - 4.f;
        angles.emplace(bitsOf(y), bitsOf(x));
        quadrants[(y < 0.f ? 2 : 0) + (x < 0.f ? 1 : 0)] = true;
        if (x == 0.f && y == 0.f) {
            continue;
        }
        const f32 gotAtan = evalProgF32(Prog{}.F(y).F(x).emit(0x5A));
        const f32 wantAtan = std::atan2(y, x);
        REQUIRE(ulp(gotAtan, wantAtan) <= 1u);
    }
    REQUIRE(powers.size() > 1900);
    REQUIRE(angles.size() > 1900);
    REQUIRE(minimumBase < 0.3f);
    REQUIRE(maximumBase > 3.9f);
    REQUIRE(minimumExponent < -3.9f);
    REQUIRE(maximumExponent > 3.9f);
    for (const bool quadrant : quadrants) {
        REQUIRE(quadrant);
    }
}
