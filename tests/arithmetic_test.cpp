#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstring>
#include <limits>
#include <string>

#include <vector>

#include "support/test_support.hpp"

#include "iprop/VM.hpp"

using namespace iprop;
using namespace test_support;

namespace {

constexpr i32 kMin = std::numeric_limits<i32>::min();
constexpr i32 kMax = std::numeric_limits<i32>::max();

} // namespace

TEST_CASE("i32 add/sub/mul wrap in two's complement") {
    REQUIRE(evalProgI32(Prog{}.I(1).I(2).emit(0x20)) == 3);
    REQUIRE(evalProgI32(Prog{}.I(kMax).I(1).emit(0x20)) == kMin);   // wraps
    REQUIRE(evalProgI32(Prog{}.I(5).I(8).emit(0x21)) == -3);
    REQUIRE(evalProgI32(Prog{}.I(kMin).I(1).emit(0x21)) == kMax);   // wraps
    REQUIRE(evalProgI32(Prog{}.I(3).I(4).emit(0x22)) == 12);
    REQUIRE(evalProgI32(Prog{}.I(0x40000000).I(4).emit(0x22)) == 0); // 2^30 * 4 wraps
    REQUIRE(evalProgI32(Prog{}.I(-2).I(3).emit(0x22)) == -6);
}

TEST_CASE("i32 division and remainder follow C truncation semantics") {
    REQUIRE(evalProgI32(Prog{}.I(10).I(3).emit(0x23)) == 3);
    REQUIRE(evalProgI32(Prog{}.I(-10).I(3).emit(0x23)) == -3);
    REQUIRE(evalProgI32(Prog{}.I(10).I(-3).emit(0x23)) == -3);
    REQUIRE(evalProgI32(Prog{}.I(-10).I(-3).emit(0x23)) == 3);

    REQUIRE(evalProgI32(Prog{}.I(10).I(3).emit(0x25)) == 1);
    REQUIRE(evalProgI32(Prog{}.I(-10).I(3).emit(0x25)) == -1);
    REQUIRE(evalProgI32(Prog{}.I(10).I(-3).emit(0x25)) == 1);
    REQUIRE(evalProgI32(Prog{}.I(-10).I(-3).emit(0x25)) == -1);
    // INT32_MIN % -1 is 0, not UB.
    REQUIRE(evalProgI32(Prog{}.I(kMin).I(-1).emit(0x25)) == 0);

    // Unsigned: 0xFFFFFFFF / 3 = 0x55555555, remainder 0.
    REQUIRE(evalProgI32(Prog{}.I(-1).I(3).emit(0x24)) == static_cast<i32>(0x55555555u));
    REQUIRE(evalProgI32(Prog{}.I(-1).I(3).emit(0x26)) == 0);
}

TEST_CASE("division and remainder by zero trap") {
    expectProgTrap(Prog{}.I(1).I(0).emit(0x23), "divide by zero");
    expectProgTrap(Prog{}.I(1).I(0).emit(0x24), "divide by zero");
    expectProgTrap(Prog{}.I(1).I(0).emit(0x25), "divide by zero");
    expectProgTrap(Prog{}.I(1).I(0).emit(0x26), "divide by zero");
}

TEST_CASE("INT32_MIN / -1 traps as integer overflow") {
    expectProgTrap(Prog{}.I(kMin).I(-1).emit(0x23), "integer overflow");
}

TEST_CASE("i32 bitwise operations") {
    REQUIRE(evalProgI32(Prog{}.I(0x0000F0F0).I(0x0000FF00).emit(0x27)) == 0x0000F000);
    REQUIRE(evalProgI32(Prog{}.I(0x00000F0F).I(0x0000F0F0).emit(0x28)) == 0x0000FFFF);
    REQUIRE(evalProgI32(Prog{}.I(0x0000FFFF).I(0x00000FF0).emit(0x29)) == 0x0000F00F);
}

TEST_CASE("i32 shifts mask the amount and match pinned semantics") {
    REQUIRE(evalProgI32(Prog{}.I(1).I(31).emit(0x2A)) == kMin);
    REQUIRE(evalProgI32(Prog{}.I(1).I(33).emit(0x2A)) == 2); // 33 & 31 = 1
    REQUIRE(evalProgI32(Prog{}.I(1).I(-1).emit(0x2A)) == kMin); // -1 & 31 = 31
    REQUIRE(evalProgI32(Prog{}.I(8).I(1).emit(0x2B)) == 4);
    REQUIRE(evalProgI32(Prog{}.I(-8).I(1).emit(0x2B)) == -4); // arithmetic shift
    REQUIRE(evalProgI32(Prog{}.I(-1).I(31).emit(0x2B)) == -1);
    REQUIRE(evalProgI32(Prog{}.I(kMin).I(1).emit(0x2C)) == 0x40000000); // logical
    REQUIRE(evalProgI32(Prog{}.I(-1).I(31).emit(0x2C)) == 1);
}

TEST_CASE("i32 neg and not wrap") {
    REQUIRE(evalProgI32(Prog{}.I(5).emit(0x2D)) == -5);
    REQUIRE(evalProgI32(Prog{}.I(kMin).emit(0x2D)) == kMin); // wraps
    REQUIRE(evalProgI32(Prog{}.I(0).emit(0x2E)) == -1);
    REQUIRE(evalProgI32(Prog{}.I(0x00000F0F).emit(0x2E)) == static_cast<i32>(0xFFFFF0F0u));
}

TEST_CASE("i32 comparisons, signed and unsigned") {
    REQUIRE(evalProgI32(Prog{}.I(5).I(5).emit(0x30)) == 1);
    REQUIRE(evalProgI32(Prog{}.I(5).I(6).emit(0x31)) == 1);
    REQUIRE(evalProgI32(Prog{}.I(-1).I(1).emit(0x32)) == 1); // lt_s
    REQUIRE(evalProgI32(Prog{}.I(-1).I(1).emit(0x33)) == 0); // lt_u: 0xFFFFFFFF > 1
    REQUIRE(evalProgI32(Prog{}.I(2).I(1).emit(0x34)) == 1); // gt_s
    REQUIRE(evalProgI32(Prog{}.I(-1).I(1).emit(0x35)) == 1); // gt_u
    REQUIRE(evalProgI32(Prog{}.I(5).I(5).emit(0x36)) == 1); // le_s
    REQUIRE(evalProgI32(Prog{}.I(1).I(-1).emit(0x37)) == 1); // le_u: 1 <= 0xFFFFFFFF
    REQUIRE(evalProgI32(Prog{}.I(5).I(5).emit(0x38)) == 1); // ge_s
    REQUIRE(evalProgI32(Prog{}.I(-1).I(1).emit(0x39)) == 1); // ge_u
}

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

TEST_CASE("conversions follow the pinned rules") {
    REQUIRE(evalProgF32(Prog{}.I(3).emit(0x4D)) == 3.f);
    REQUIRE(evalProgF32(Prog{}.I(-3).emit(0x4D)) == -3.f);

    REQUIRE(evalProgI32(Prog{}.F(3.7f).emit(0x4E)) == 3);   // truncate
    REQUIRE(evalProgI32(Prog{}.F(-3.7f).emit(0x4E)) == -3); // truncate toward zero
    const f32 nan = std::numeric_limits<f32>::quiet_NaN();
    REQUIRE(evalProgI32(Prog{}.F(nan).emit(0x4E)) == 0);            // NaN -> 0
    const f32 inf = std::numeric_limits<f32>::infinity();
    REQUIRE(evalProgI32(Prog{}.F(inf).emit(0x4E)) == kMax);         // +inf saturates
    REQUIRE(evalProgI32(Prog{}.F(-inf).emit(0x4E)) == kMin);        // -inf saturates
    REQUIRE(evalProgI32(Prog{}.F(1.0e20f).emit(0x4E)) == kMax);     // out of range
}

TEST_CASE("f32.sin and f32.cos match the pinned algorithm bit for bit") {
    // Spec §12.7: the Cody-Waite quadrant reduction plus the Taylor
    // polynomials, all in double precision. The expected bit patterns below
    // are the frozen outputs of that algorithm — any change is a breaking
    // change (regenerate only together with a spec update).
    struct Case {
        u32 input; // the f32 bits
        u32 sinBits;
        u32 cosBits;
    };
    const Case cases[] = {
        {0x00000000u, 0x00000000u, 0x3F800000u}, // zero
        {0x80000000u, 0x80000000u, 0x3F800000u}, // -0: sin(-0) = -0
        {0x3F490FDBu, 0x3F3504F3u, 0x3F3504F3u}, // pi/4
        {0x3FC90FDBu, 0x3F800000u, 0xB33BBD2Eu}, // pi/2
        {0x40490FDBu, 0xB3BBBD2Eu, 0xBF800000u}, // pi
        {0x4096CBE4u, 0xBF800000u, 0x324CDE2Eu}, // 3pi/2
        {0x40C90FDBu, 0x343BBD2Eu, 0x3F800000u}, // 2pi
        {0xBFC90FDBu, 0xBF800000u, 0xB33BBD2Eu}, // -pi/2
        {0x3F800000u, 0x3F576AA4u, 0x3F0A5140u}, // 1
        {0xBF800000u, 0xBF576AA4u, 0x3F0A5140u}, // -1
        {0x3F000000u, 0x3EF57744u, 0x3F60A940u}, // 0.5
        {0x358637BDu, 0x358637BDu, 0x3F800000u}, // 1e-6: sin(x) = x, cos(x) = 1
        {0x42C80000u, 0xBF01A12Eu, 0x3F5CC0EEu}, // 100 (reduction path)
        {0x447A0000u, 0x3F53AE61u, 0x3F0FF813u}, // 1000
        {0x501502F9u, 0xBEF99A60u, 0x3F5F84C6u}, // 1e10 (large, still pinned)
        {0x3F333333u, 0x3F24EB73u, 0x3F43CCB3u}, // 0.7
        {0x3EE4DD2Fu, 0x3EDD517Au, 0x3F66D8F4u}, // 0.447
    };
    for (const Case& testCase : cases) {
        INFO("input 0x" << std::hex << testCase.input);
        REQUIRE(bitsOf(evalProgF32(Prog{}.F(f32FromBits(testCase.input)).emit(0x4F))) ==
                testCase.sinBits);
        REQUIRE(bitsOf(evalProgF32(Prog{}.F(f32FromBits(testCase.input)).emit(0x53))) ==
                testCase.cosBits);
    }

    // NaN and infinities yield the canonical quiet NaN (0x7FC00000).
    const u32 canonical = 0x7FC00000u;
    const f32 nan = std::numeric_limits<f32>::quiet_NaN();
    const f32 inf = std::numeric_limits<f32>::infinity();
    REQUIRE(bitsOf(evalProgF32(Prog{}.F(nan).emit(0x4F))) == canonical);
    REQUIRE(bitsOf(evalProgF32(Prog{}.F(nan).emit(0x53))) == canonical);
    REQUIRE(bitsOf(evalProgF32(Prog{}.F(inf).emit(0x4F))) == canonical);
    REQUIRE(bitsOf(evalProgF32(Prog{}.F(-inf).emit(0x53))) == canonical);
}

TEST_CASE("the pinned sine agrees with libm to within 1 ulp on a grid") {
    // The pinned algorithm is not required to be correctly rounded — the
    // spec only pins its exact output — but on the operating envelope
    // (|x| <= 4096) it must be within 1 ulp of the mathematical value. The
    // comparison against std::sin/std::cos (correctly rounded on macOS
    // libm) bounds the polynomial and the reduction.
    const auto ulp = [](f32 a, f32 b) {
        if (std::isnan(a) && std::isnan(b)) {
            return 0u;
        }
        const u32 x = bitsOf(a);
        const u32 y = bitsOf(b);
        const u32 diff = x > y ? x - y : y - x;
        return diff;
    };
    for (u32 i = 0; i < 2000; ++i) {
        // Deterministic pseudo-random f32s in [-4096, 4096], avoiding NaN.
        u32 bits = i * 2654435761u + 0x9e3779b9u;
        bits &= 0x7FFFFFu; // 23 mantissa bits
        f32 value = f32FromBits(bits);
        value *= (i % 2 == 0 ? 1.f : -1.f);
        value *= (i % 17 == 0 ? 4096.f : 16.f);
        const f32 gotSin = evalProgF32(Prog{}.F(value).emit(0x4F));
        const f32 gotCos = evalProgF32(Prog{}.F(value).emit(0x53));
        const f32 wantSin = std::sin(value);
        const f32 wantCos = std::cos(value);
        INFO("value " << value);
        REQUIRE(ulp(gotSin, wantSin) <= 1u);
        REQUIRE(ulp(gotCos, wantCos) <= 1u);
    }
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
    const auto ulp = [](f32 a, f32 b) {
        if (std::isnan(a) && std::isnan(b)) {
            return 0u;
        }
        if (std::isinf(a) && std::isinf(b) && std::signbit(a) == std::signbit(b)) {
            return 0u;
        }
        const u32 x = bitsOf(a);
        const u32 y = bitsOf(b);
        return x > y ? x - y : y - x;
    };
    for (u32 i = 0; i < 2000; ++i) {
        const u32 bits = i * 2654435761u + 0x9e3779b9u;
        f32 value = f32FromBits((bits & 0x7FFFFFu) | 0x3F000000u); // in [0.5, 1)
        value *= (i % 2 == 0 ? 1.f : -1.f);
        value *= (i % 17 == 0 ? 8.f : 1.f);
        const f32 gotExp = evalProgF32(Prog{}.F(value).emit(0x59));
        const f32 wantExp = std::exp(value);
        REQUIRE(ulp(gotExp, wantExp) <= 1u);

        // pow over positive bases and moderate exponents.
        const f32 base = 0.25f + f32FromBits(bits & 0x3FFFFFu) * 0.5f;
        const f32 exponent = f32FromBits((bits >> 8) & 0x3FFFFFu) * 2.f - 4.f;
        const f32 gotPow = evalProgF32(Prog{}.F(base).F(exponent).emit(0x58));
        const f32 wantPow = std::pow(base, exponent);
        REQUIRE(ulp(gotPow, wantPow) <= 2u);

        // atan2 over the full circle.
        const f32 y = f32FromBits(bits & 0x7FFFFFu) * 8.f - 4.f;
        const f32 x = f32FromBits((bits >> 16) & 0x7FFFFFu) * 8.f - 4.f;
        if (x == 0.f && y == 0.f) {
            continue;
        }
        const f32 gotAtan = evalProgF32(Prog{}.F(y).F(x).emit(0x5A));
        const f32 wantAtan = std::atan2(y, x);
        REQUIRE(ulp(gotAtan, wantAtan) <= 1u);
    }
}

TEST_CASE("color.shade clamps channels and keeps alpha") {
    const Color c{100, 150, 200, 255};
    REQUIRE(evalProgColor(Prog{}.C(c).I(50).emit(0x51)) == Color{150, 200, 250, 255});
    REQUIRE(evalProgColor(Prog{}.C(c).I(300).emit(0x51)) == Color{255, 255, 255, 255});
    REQUIRE(evalProgColor(Prog{}.C(c).I(-300).emit(0x51)) == Color{0, 0, 0, 255});
    const Color semi{10, 20, 30, 77};
    REQUIRE(evalProgColor(Prog{}.C(semi).I(5).emit(0x51)) == Color{15, 25, 35, 77});
}

TEST_CASE("color.mix lerps RGB, clamps t and copies the first alpha") {
    const Color a{0, 0, 0, 255};
    const Color b{100, 200, 50, 255};
    REQUIRE(evalProgColor(Prog{}.C(a).C(b).F(0.5f).emit(0x50)) == Color{50, 100, 25, 255});
    REQUIRE(evalProgColor(Prog{}.C(a).C(b).F(2.f).emit(0x50)) == Color{100, 200, 50, 255}); // t clamped
    REQUIRE(evalProgColor(Prog{}.C(a).C(b).F(-1.f).emit(0x50)) == Color{0, 0, 0, 255});      // t clamped

    const Color src{1, 2, 3, 7};
    const Color other{9, 9, 9, 9};
    REQUIRE(evalProgColor(Prog{}.C(src).C(other).F(0.f).emit(0x50)) == Color{1, 2, 3, 7});
}

TEST_CASE("color.mix uses the pinned fused multiply-add") {
    // The QuantSpace reference build contracts x*(1-t)+y*t into an FMA; the
    // pinned formula reproduces its output exactly. This vector is
    // contraction-sensitive: unfused it truncates to 196, fused to 197.
    const Color hair{208, 209, 199};
    const Color skin{158, 101, 71};
    const auto result = evalProgColor(Prog{}.C(hair).C(skin).F(0.22f).emit(0x50));
    REQUIRE(result.r == 197);
    REQUIRE(result.g == 185);
    REQUIRE(result.b == 170);
}
