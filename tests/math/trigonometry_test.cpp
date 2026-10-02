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
