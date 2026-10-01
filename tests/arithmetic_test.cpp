#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <set>
#include <string>

#include <vector>

#include "support/test_support.hpp"

#include "iprop/VM.hpp"

using namespace iprop;
using namespace test_support;

namespace {

constexpr i32 kMin = std::numeric_limits<i32>::min();
constexpr i32 kMax = std::numeric_limits<i32>::max();

f32 unitFromBits(u32 bits) {
    return static_cast<f32>(bits >> 8) * 0x1p-24f;
}

u32 ulp(f32 a, f32 b) {
    if (a == b || (std::isnan(a) && std::isnan(b))) {
        return 0;
    }
    const auto ordered = [](f32 value) {
        const u32 bits = bitsOf(value);
        return (bits & 0x80000000u) != 0 ? ~bits : bits | 0x80000000u;
    };
    const u32 x = ordered(a);
    const u32 y = ordered(b);
    return x > y ? x - y : y - x;
}

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
    std::set<u32> samples;
    f32 minimum = 4096.f;
    f32 maximum = -4096.f;
    for (u32 i = 0; i < 2000; ++i) {
        const u32 bits = i * 2654435761u + 0x9e3779b9u;
        const f32 value = unitFromBits(bits) * 8192.f - 4096.f;
        samples.insert(bitsOf(value));
        minimum = std::min(minimum, value);
        maximum = std::max(maximum, value);
        const f32 gotSin = evalProgF32(Prog{}.F(value).emit(0x4F));
        const f32 gotCos = evalProgF32(Prog{}.F(value).emit(0x53));
        const f32 wantSin = std::sin(value);
        const f32 wantCos = std::cos(value);
        INFO("value " << value);
        REQUIRE(ulp(gotSin, wantSin) <= 1u);
        REQUIRE(ulp(gotCos, wantCos) <= 1u);
    }
    REQUIRE(samples.size() > 1900);
    REQUIRE(minimum < -4000.f);
    REQUIRE(maximum > 4000.f);
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
    // rot({1, 0}, a) must equal {cos(a), sin(a)} from the same pinned
    // algorithms the f32.cos/f32.sin opcodes use (spec §12.7).
    const f32 angle = 1.2345f;
    const auto rotated = std::get<Vec2>(
        evalProg(Prog{}.F(1.f).F(0.f).emit(0x5B).F(angle).emit(0x77))[0]);
    REQUIRE(rotated.x == evalProgF32(Prog{}.F(angle).emit(0x53))); // f32.cos
    REQUIRE(rotated.y == evalProgF32(Prog{}.F(angle).emit(0x4F))); // f32.sin
    // A quarter turn of {1, 0} lands near {0, 1} (the pinned cos(pi/2)
    // is tiny but not exactly zero).
    const f32 piOver2 = 1.57079632679f;
    const auto quarter = std::get<Vec2>(
        evalProg(Prog{}.F(1.f).F(0.f).emit(0x5B).F(piOver2).emit(0x77))[0]);
    REQUIRE(std::fabs(quarter.x) < 1e-5f);
    REQUIRE(quarter.y == evalProgF32(Prog{}.F(piOver2).emit(0x4F)));
    // And the rotation of {0, 1} by pi/2 lands on {-1, 0}-ish: the
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

TEST_CASE("the noise v2 matches the pinned spec vectors bit for bit") {
    // The seed operand is 7 throughout (§12.10's vectors).
    REQUIRE(bitsOf(evalProgF32(Prog{}.F(0.f).F(0.f).I(7).emit(0x89))) ==
            0x00000000u);
    REQUIRE(bitsOf(evalProgF32(Prog{}.F(1.5f).F(2.5f).I(7).emit(0x89))) ==
            0x3E800000u);
    REQUIRE(bitsOf(evalProgF32(Prog{}.F(-3.25f).F(4.75f).I(7).emit(0x89))) ==
            0xBE1E1180u);
    REQUIRE(bitsOf(evalProgF32(Prog{}.F(0.5f).F(0.5f).I(4).I(7).emit(0x8A))) ==
            0x3E088889u);
    REQUIRE(bitsOf(evalProgF32(Prog{}.F(1.5f).F(2.5f).I(3).I(7).emit(0x8A))) ==
            0x3E124925u);
    REQUIRE(bitsOf(evalProgF32(Prog{}.F(0.5f).F(0.5f).I(7).emit(0x8B))) ==
            0x3E14D03Eu);
    REQUIRE(bitsOf(evalProgF32(Prog{}.F(-3.25f).F(4.75f).I(7).emit(0x8B))) ==
            0x3F19A4ABu);
    REQUIRE(bitsOf(evalProgF32(
                Prog{}.F(0.5f).F(0.5f).F(0.2f).I(7).emit(0x8C))) == 0x3E29272Cu);
    const auto jitter = std::get<Vec2>(evalProg(Prog{}.I(3).I(4).I(7).emit(0x8D))[0]);
    REQUIRE(bitsOf(jitter.x) == 0x407DE0B8u);
    REQUIRE(bitsOf(jitter.y) == 0x40820079u);
    REQUIRE(bitsOf(evalProgF32(Prog{}.F(0.5f).F(0.5f).I(4).I(7).emit(0x8E))) ==
            0x3F444444u);
    // The octave clamp: 0 -> 1 (the first octave only), 9 -> 8, -3 -> 1.
    REQUIRE(bitsOf(evalProgF32(Prog{}.F(0.5f).F(0.5f).I(0).I(7).emit(0x8A))) ==
            0x3E800000u);
    REQUIRE(bitsOf(evalProgF32(Prog{}.F(0.5f).F(0.5f).I(9).I(7).emit(0x8A))) ==
            bitsOf(evalProgF32(Prog{}.F(0.5f).F(0.5f).I(8).I(7).emit(0x8A))));
    REQUIRE(bitsOf(evalProgF32(Prog{}.F(0.5f).F(0.5f).I(-3).I(7).emit(0x8A))) ==
            0x3E800000u);
}

TEST_CASE("the noise v2 values stay inside their pinned ranges") {
    std::set<std::pair<f32, f32>> coordinates;
    bool positiveGradient = false;
    bool negativeGradient = false;
    for (u32 i = 0; i < 300; ++i) {
        const u32 mix = i * 2654435761u + 0x9e3779b9u;
        const f32 x = unitFromBits(mix) * 8.f - 4.f;
        const f32 y = unitFromBits(mix * 2246822519u + 3266489917u) * 8.f - 4.f;
        coordinates.emplace(x, y);
        const f32 g = evalProgF32(Prog{}.F(x).F(y).I(7).emit(0x89));
        positiveGradient |= g > 0.f;
        negativeGradient |= g < 0.f;
        REQUIRE((g >= -1.f && g <= 1.f));
        const f32 f = evalProgF32(Prog{}.F(x).F(y).I(4).I(7).emit(0x8A));
        REQUIRE((f >= -1.f && f <= 1.f));
        const f32 c = evalProgF32(Prog{}.F(x).F(y).I(7).emit(0x8B));
        REQUIRE(c >= 0.f);
        const f32 r = evalProgF32(Prog{}.F(x).F(y).I(4).I(7).emit(0x8E));
        REQUIRE((r >= 0.f && r <= 1.f));
    }
    REQUIRE(coordinates.size() == 300);
    REQUIRE(positiveGradient);
    REQUIRE(negativeGradient);
}

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

TEST_CASE("the colour ops match the pinned spec vectors") {
    REQUIRE(evalProgColor(Prog{}.C(Color{0x11, 0x22, 0x33, 0x44}).I(128).emit(0x52)) ==
            Color{0x11, 0x22, 0x33, 0x80});
    REQUIRE(evalProgColor(Prog{}.C(Color{0x11, 0x22, 0x33, 0x44}).I(-5).emit(0x52)) ==
            Color{0x11, 0x22, 0x33, 0x00});
    REQUIRE(evalProgColor(Prog{}.C(Color{0x80, 0x80, 0x80, 0xff})
                                .C(Color{0x80, 0x80, 0x80, 0xff}).emit(0x9A)) ==
            Color{0x40, 0x40, 0x40, 0xff});
    REQUIRE(evalProgColor(Prog{}.C(Color{0xff, 0x80, 0x40, 0xff})
                                .C(Color{0x40, 0x80, 0xff, 0xff}).emit(0x9A)) ==
            Color{0x40, 0x40, 0x40, 0xff});
    REQUIRE(evalProgColor(Prog{}.C(Color{0x80, 0xa0, 0xc0, 0xff}).F(0.5f).emit(0x9B)) ==
            Color{0x40, 0x50, 0x60, 0xff});
    REQUIRE(evalProgColor(Prog{}.C(Color{0x80, 0x40, 0x40, 0xff}).F(2.5f).emit(0x9B)) ==
            Color{0xff, 0xa0, 0xa0, 0xff});
    REQUIRE(evalProgColor(Prog{}.C(Color{255, 0, 0, 255})
                                .F(1.f / 6.f).F(1.f).F(1.f).emit(0x9C)) ==
            Color{0xff, 0xff, 0x00, 0xff});
    REQUIRE(evalProgColor(Prog{}.C(Color{255, 0, 0, 255})
                                .F(1.f / 3.f).F(1.f).F(1.f).emit(0x9C)) ==
            Color{0x00, 0xff, 0x00, 0xff});
    REQUIRE(evalProgColor(Prog{}.C(Color{255, 0, 0, 255})
                                .F(0.f).F(0.f).F(1.f).emit(0x9C)) ==
            Color{0xff, 0xff, 0xff, 0xff}); // the desaturation at v=1
    REQUIRE(evalProgColor(Prog{}.C(Color{255, 0, 0, 255})
                                .F(0.f).F(1.f).F(0.5f).emit(0x9C)) ==
            Color{0x7f, 0x00, 0x00, 0xff});
    REQUIRE(evalProgColor(Prog{}.C(Color{0x33, 0x66, 0x99, 0xff})
                                .F(0.75f).F(0.5f).F(1.5f).emit(0x9C)) ==
            Color{0x99, 0xe5, 0x99, 0xff});
}

TEST_CASE("neutral HSV parameters preserve RGB within one truncation byte and keep alpha") {
    const Color orange{255, 128, 0, 77};
    REQUIRE(evalProgColor(Prog{}.C(orange).F(0.f).F(1.f).F(1.f).emit(0x9C)) == orange);
    // Cover achromatic colours and every RGB-to-HSV branch. A binary32
    // round-trip can lose one channel unit when the final conversion truncates.
    for (const u8 r : {0, 17, 64, 128, 192, 255}) {
        for (const u8 g : {0, 17, 64, 128, 192, 255}) {
            for (const u8 b : {0, 17, 64, 128, 192, 255}) {
                const Color source{r, g, b, 77};
                const Color result = evalProgColor(
                    Prog{}.C(source).F(0.f).F(1.f).F(1.f).emit(0x9C));
                CAPTURE(r, g, b);
                REQUIRE(std::abs(static_cast<int>(result.r) - r) <= 1);
                REQUIRE(std::abs(static_cast<int>(result.g) - g) <= 1);
                REQUIRE(std::abs(static_cast<int>(result.b) - b) <= 1);
                REQUIRE(result.a == source.a);
            }
        }
    }
}

TEST_CASE("HSV hue interpolates within every sector and wraps both directions") {
    const Color red{255, 0, 0, 77};
    const Color midpoints[] = {
        {255, 127, 0, 77}, {127, 255, 0, 77}, {0, 255, 127, 77},
        {0, 127, 255, 77}, {127, 0, 255, 77}, {255, 0, 127, 77},
    };
    for (unsigned sector = 0; sector < 6; ++sector) {
        const f32 hue = static_cast<f32>(2 * sector + 1) / 12.f;
        for (const f32 turns : {-1.f, 0.f, 1.f}) {
            CAPTURE(sector, turns);
            REQUIRE(evalProgColor(Prog{}.C(red).F(hue + turns).F(1.f).F(1.f).emit(0x9C)) ==
                    midpoints[sector]);
        }
    }
}

TEST_CASE("HSV traps on nonfinite hue shifts before converting the sector") {
    const f32 inf = std::numeric_limits<f32>::infinity();
    for (const f32 hue : {inf, -inf, f32FromBits(0x7FC01234u), f32FromBits(0xFFC01234u)}) {
        for (const Color c : {Color{255, 0, 0, 77}, Color{128, 128, 128, 77}, Color{0, 0, 0, 77}}) {
            CAPTURE(hue, c.r, c.g, c.b);
            expectProgTrap(Prog{}.C(c).F(hue).F(1.f).F(1.f).emit(0x9C),
                           "color.hsv hue shift must be finite");
        }
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
