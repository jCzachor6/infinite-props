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

TEST_CASE("dither matches the pinned Bayer thresholds") {
    // (0,0) -> 0 -> the threshold 0.03125; the >= comparison.
    REQUIRE(evalProgF32(Prog{}.F(0.f).I(0).I(0).emit(0x9D)) == 0.f);
    REQUIRE(evalProgF32(Prog{}.F(0.03125f).I(0).I(0).emit(0x9D)) == 1.f);
    // (1,0) -> 8 -> the threshold 0.53125.
    REQUIRE(evalProgF32(Prog{}.F(0.53125f).I(1).I(0).emit(0x9D)) == 1.f);
    REQUIRE(evalProgF32(Prog{}.F(0.5f).I(1).I(0).emit(0x9D)) == 0.f);
    // The negative coordinates wrap through the u32 mask.
    REQUIRE(evalProgF32(Prog{}.F(0.f).I(-4).I(0).emit(0x9D)) == 0.f);
    REQUIRE(evalProgF32(Prog{}.F(0.03125f).I(-4).I(0).emit(0x9D)) == 1.f);
}

TEST_CASE("color.shade clamps channels and keeps alpha") {
    const Color c{100, 150, 200, 255};
    REQUIRE(evalProgColor(Prog{}.C(c).I(50).emit(0x51)) == Color{150, 200, 250, 255});
    REQUIRE(evalProgColor(Prog{}.C(c).I(300).emit(0x51)) == Color{255, 255, 255, 255});
    REQUIRE(evalProgColor(Prog{}.C(c).I(-300).emit(0x51)) == Color{0, 0, 0, 255});
    const Color semi{10, 20, 30, 77};
    REQUIRE(evalProgColor(Prog{}.C(semi).I(5).emit(0x51)) == Color{15, 25, 35, 77});
    for (const auto amount : {kMax, kMax-255})
        CHECK(evalProgColor(Prog{}.C(semi).I(amount).emit(0x51)) == Color{255,255,255,77});
    for (const auto amount : {kMin, kMin+255})
        CHECK(evalProgColor(Prog{}.C(semi).I(amount).emit(0x51)) == Color{0,0,0,77});
}

TEST_CASE("color.mix handles the shared float corpus without undefined conversions") {
    const Color a{0,128,255,73}, b{255,128,0,212};
    for (const auto t : f32BoundaryCorpus()) {
        INFO("t bits " << bitsOf(t));
        const auto result = evalProgColor(Prog{}.C(a).C(b).F(t).emit(0x50));
        CHECK(result.a == a.a);
        if (std::isnan(t) || t <= 0.f) CHECK(result == a);
        else if (t >= 1.f) CHECK(result == Color{b.r,b.g,b.b,a.a});
        else {
            CHECK(result.r == static_cast<u8>(255.f * t));
            CHECK(result.b == static_cast<u8>(255.f * (1.f-t)));
            CHECK(result.g == 128);
        }
    }
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
