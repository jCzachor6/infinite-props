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

TEST_CASE("i32 bounds keep signed values beyond binary32 precision") {
    struct Pair { i32 a, b, low, high; };
    const Pair pairs[] = {
        {kMin, kMax, kMin, kMax}, {kMax, kMin, kMin, kMax},
        {kMax - 1, kMax, kMax - 1, kMax}, {kMin + 1, kMin, kMin, kMin + 1},
        {16777217, 16777218, 16777217, 16777218},
        {-16777217, -16777218, -16777218, -16777217},
        {-1, 0, -1, 0}, {0, -1, -1, 0}, {42, 42, 42, 42},
    };
    for (const auto& p : pairs) {
        INFO(p.a << ", " << p.b);
        CHECK(evalProgI32(Prog{}.I(p.a).I(p.b).emit(0xB1)) == p.low);
        CHECK(evalProgI32(Prog{}.I(p.a).I(p.b).emit(0xB2)) == p.high);
    }
    struct Clamp { i32 x, a, b, expected; };
    const Clamp clamps[] = {
        {0, kMin, kMax, 0}, {kMin, kMax, kMin, kMin}, {kMax, kMax, kMin, kMax},
        {16777217, 16777216, 16777218, 16777217},
        {kMax, 16777218, 16777217, 16777218},
        {kMin, -16777217, -16777218, -16777218},
        {kMin, kMax, kMax, kMax}, {kMax, kMin, kMin, kMin},
        {5, 10, -10, 5}, {-20, 10, -10, -10}, {20, 10, -10, 10},
    };
    for (const auto& p : clamps) {
        INFO(p.x << ", " << p.a << ", " << p.b);
        CHECK(evalProgI32(Prog{}.I(p.x).I(p.a).I(p.b).emit(0xB3)) == p.expected);
    }
}

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
