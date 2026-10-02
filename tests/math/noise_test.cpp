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
