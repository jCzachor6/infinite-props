#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include "Compiler.hpp"
#include "iprop/VM.hpp"
#include "iprop/math/Color.hpp"
#include "iprop/math/Vector.hpp"
#include "ips/Parser.hpp"
#include "ips/Printer.hpp"
#include "ips/Semantics.hpp"
#include "ips/SourceError.hpp"

#include "support/compiler_support.hpp"
#include "support/test_support.hpp"

// Color gradients and vec2 segments drawn with `to` (spec v0.5 §5).

using namespace iprop;

namespace {
constexpr u8 kColorMixRgba = 0xAF, kVec2Mix = 0xB0, kVec2Make = 0x5B;

std::vector<u8> compileSource(const std::string& source, bool optimize) {
    const auto program = ips::parse(source);
    ips::checkAsset(program, {});
    return compileProgram(program, {}, optimize);
}

GenerationResult generate(const std::vector<u8>& bytes, u32 seed, u32 variant = 0) {
    const auto loaded = loadFile(bytes);
    validate(loaded);
    VM vm(loaded, GenerationInputs{seed, 0, 0, {{"v", {InputType::U32, {}, 0.f, variant}}}});
    vm.run();
    return vm.result();
}

std::vector<u32> pointBits(const GenerationResult& result) {
    std::vector<u32> bits;
    for (const auto& output : result.outputs)
        for (const auto& point : output.points) {
            bits.push_back(std::bit_cast<u32>(point.x));
            bits.push_back(std::bit_cast<u32>(point.y));
        }
    return bits;
}

std::string sourceError(const std::string& source) {
    try {
        ips::checkAsset(ips::parse(source), {});
    } catch (const ips::SourceError& error) {
        return error.message;
    }
    FAIL("expected a source error");
    return {};
}

Vec2 vec2Mix(Vec2 a, Vec2 b, f32 t) {
    test_support::Prog prog;
    prog.F(a.x).F(a.y).emit(kVec2Make).F(b.x).F(b.y).emit(kVec2Make).F(t).emit(kVec2Mix);
    const auto stack = test_support::evalProg(prog);
    REQUIRE(stack.size() == 1);
    return std::get<Vec2>(stack[0]);
}
} // namespace

TEST_CASE("color.mix.rgba mixes all four channels with the color.mix formula") {
    const std::vector<Color> colors = {{0, 0, 0, 0}, {255, 255, 255, 255}, {0x40, 0xa0, 0xff, 0x40},
                                       {0x70, 0x58, 0x45, 0xff}, {0xc8, 0xc8, 0xc8, 0x80}, {1, 254, 2, 253}};
    const std::vector<f32> positions = {0.f, 1.f, 0.5f, 0.25f, 0.1f, 0.9999999f, 1e-7f, 0.3f};
    for (const auto a : colors)
        for (const auto b : colors)
            for (const f32 t : positions) {
                const Color mixed = test_support::evalProgColor(test_support::Prog{}.C(a).C(b).F(t).emit(kColorMixRgba));
                CHECK(mixed == math::colorMixRgba(a, b, t));
                // RGB is color.mix up to the end clamp; alpha follows the same rule.
                const Color legacy = math::colorMix(a, b, t);
                const auto between = [](u8 v, u8 x, u8 y) { return v >= std::min(x, y) && v <= std::max(x, y); };
                CHECK(between(mixed.r, a.r, b.r));
                CHECK(between(mixed.a, a.a, b.a));
                CHECK(std::abs(mixed.r - legacy.r) <= 1);
                CHECK(std::abs(mixed.b - legacy.b) <= 1);
                if (a == b) CHECK(mixed == a);
            }
    CHECK(math::colorMixRgba({0, 0, 0, 0}, {255, 255, 255, 255}, 1.f) == Color{255, 255, 255, 255});
    CHECK(math::colorMixRgba({0, 0, 0, 0}, {255, 255, 255, 255}, 0.5f) == Color{127, 127, 127, 127});
    // t clamps like color.mix: NaN and negative values select the first color.
    CHECK(math::colorMixRgba({10, 20, 30, 40}, {50, 60, 70, 80}, std::numeric_limits<f32>::quiet_NaN()) ==
          Color{10, 20, 30, 40});
    CHECK(math::colorMixRgba({10, 20, 30, 40}, {50, 60, 70, 80}, 7.f) == Color{50, 60, 70, 80});
    // color.mix itself still keeps the first alpha.
    CHECK(math::colorMix({0, 0, 0, 0x40}, {255, 255, 255, 0xff}, 1.f).a == 0x40);
}

TEST_CASE("vec2.mix stays on the segment and rejects non-finite ends") {
    CHECK(vec2Mix({10.f, 20.f}, {30.f, 40.f}, 0.f) == Vec2{10.f, 20.f});
    CHECK(vec2Mix({10.f, 20.f}, {30.f, 40.f}, 1.f) == Vec2{30.f, 40.f});
    CHECK(vec2Mix({10.f, 20.f}, {30.f, 40.f}, 0.5f) == Vec2{20.f, 30.f});
    CHECK(vec2Mix({30.f, 20.f}, {10.f, 40.f}, 0.25f) == Vec2{25.f, 25.f});
    constexpr f32 big = std::numeric_limits<f32>::max();
    for (const f32 t : {0.f, 0.1f, 0.5f, 0.9f, 1.f}) {
        const Vec2 wide = vec2Mix({-big, big}, {big, -big}, t);
        CHECK(std::isfinite(wide.x));
        CHECK(std::isfinite(wide.y));
        const Vec2 same = vec2Mix({big, 0.1f}, {big, 0.1f}, t);
        CHECK(same == Vec2{big, 0.1f});
    }
    CHECK(vec2Mix({0.f, 0.f}, {1.f, 1.f}, std::numeric_limits<f32>::quiet_NaN()) == Vec2{0.f, 0.f});
    constexpr f32 inf = std::numeric_limits<f32>::infinity(), nan = std::numeric_limits<f32>::quiet_NaN();
    for (const auto& [a, b] : std::vector<std::pair<Vec2, Vec2>>{{{inf, 0.f}, {0.f, 0.f}}, {{0.f, 0.f}, {0.f, nan}},
                                                              {{0.f, -inf}, {1.f, 1.f}}, {{nan, 0.f}, {nan, 0.f}}}) {
        test_support::Prog prog;
        prog.F(a.x).F(a.y).emit(kVec2Make).F(b.x).F(b.y).emit(kVec2Make).F(0.5f).emit(kVec2Mix);
        test_support::expectProgTrap(prog, "vec2 segment ends must be finite");
    }
}

TEST_CASE("a color gradient shares one position across all channels") {
    const std::string source = R"(asset t {
  size 1x1
  output list point gray
  output list point fade
  draw {
    for i in 0..400 {
      c = #000000ff to #ffffffff
      gray = {c.r - c.g, c.g - c.b + (c.a - 255)}
      e = #40404040 to #ffffffff
      fade = {e.a - e.r, float(e.r)}
    }
  }
})";
    for (const bool optimize : {false, true}) {
        const auto bytes = compileSource(source, optimize);
        CHECK(bytes[6] == 0);
        const auto result = generate(bytes, 13);
        for (const auto& point : result.outputs[0].points) {
            CHECK(point.x == 0.f);
            CHECK(point.y == 0.f);
        }
        f32 low = 255.f, high = 0.f;
        for (const auto& point : result.outputs[1].points) {
            CHECK(point.x == 0.f); // alpha moves with RGB
            low = std::min(low, point.y);
            high = std::max(high, point.y);
        }
        CHECK(low < 0x50);
        CHECK(high > 0xf0);
    }
}

TEST_CASE("gradients and segments draw once, after both ends and the distribution") {
    const auto program = [](const std::string& body) {
        return "asset t { size 4x1 output list point p\n"
               "  ink(x: i32) -> color { pixel(x, 0, #fff) return rgba(index(256), 0, 0, 255) }\n"
               "  at(x: i32) -> vec2 { pixel(x, 0, #fff) return {unit(), 1.0} }\n"
               "  shape() -> distribution { pixel(3, 0, #f00) return beta(alpha=2.0, beta=1.0 + unit()) }\n"
               "  draw { " + body + " p = {index(1000), unit()} } }";
    };
    const std::vector<std::pair<std::string, std::string>> equivalents = {
        {"c = ink(0) to ink(1) p = {c.r, c.a}", "low = ink(0) high = ink(1) c = low to high p = {c.r, c.a}"},
        {"q = at(0) to at(1) with shape() p = q", "low = at(0) high = at(1) d = shape() q = low to high with d p = q"},
        // One draw: the same stream position as one float range.
        {"c = #000 to #fff p = {0, 0}", "t = 0.0 to 1.0 p = {0, 0}"},
        {"q = {0, 0} to {1, 1} with shape() p = {0, 0}", "d = shape() t = 0.0 to 1.0 with d p = {0, 0}"},
        // Equal ends still draw.
        {"c = #123 to #123 p = {c.r, 0}", "t = 0.0 to 1.0 c = #123 p = {c.r, 0}"},
    };
    for (const auto& [direct, stored] : equivalents) {
        INFO(direct);
        for (const bool optimize : {false, true})
            for (const u32 seed : {0u, 7u, 26u})
                CHECK(pointBits(generate(compileSource(program(direct), optimize), seed)) ==
                      pointBits(generate(compileSource(program(stored), false), seed)));
    }
}

TEST_CASE("segments keep their direction, accept distributions and feed points") {
    const std::string source = R"(asset t {
  size 64x64
  output list point line
  output list point unit
  output list point skew
  output point last
  draw {
    distribution nearEnd = beta(alpha=5.0, beta=2.0)
    for i in 0..300 {
      line = {10.0, 20.0} to {30, 40}
      unit = {0.0, 0.0} to {1.0, 1.0}
      skew = {30.0, 40.0} to {10.0, 20.0} with nearEnd
    }
    position = {12.0, 48.0} to {40.0, 56.0} with gaussian(mean=0.5, sigma=0.15)
    last = position
    pixel(int(position.x), int(position.y), #fff)
  }
})";
    for (const bool optimize : {false, true}) {
        const auto result = generate(compileSource(source, optimize), 21);
        for (const auto& point : result.outputs[0].points) {
            CHECK(point.x >= 10.f);
            CHECK(point.x <= 30.f);
            CHECK(std::fabs((point.y - point.x) - 10.f) <= 1e-5f); // on the segment
        }
        for (const auto& point : result.outputs[1].points) {
            CHECK(point.x == point.y);
            CHECK(point.x >= 0.f);
            CHECK(point.x <= 1.f);
        }
        // beta(5, 2) favours its high end, which is the second end {10, 20}.
        const auto& skew = result.outputs[2].points;
        const auto low = std::count_if(skew.begin(), skew.end(), [](const auto& p) { return p.x < 20.f; });
        CHECK(low > 200);
        const auto last = result.outputs[3].points.at(0);
        CHECK(last.x >= 12.f);
        CHECK(last.x <= 40.f);
    }
    // Independent components are two explicit draws.
    const auto twice = generate(compileSource(
        "asset t { size 1x1 output list point p draw { for i in 0..64 { p = {0.0 to 1.0, 0.0 to 1.0} } } }", true), 5);
    CHECK(std::any_of(twice.outputs[0].points.begin(), twice.outputs[0].points.end(),
                      [](const auto& p) { return p.x != p.y; }));
}

TEST_CASE("gradient and segment types are checked and traps keep their reason") {
    const auto wrap = [](const std::string& body) { return "asset t { size 1x1 draw { " + body + " } }"; };
    CHECK(sourceError(wrap("x = #fff to {0, 0}")) ==
          "'to' needs two numbers, two colors or two vec2 values, got color and vec2");
    CHECK(sourceError(wrap("x = {0, 0} to 1.0")) ==
          "'to' needs two numbers, two colors or two vec2 values, got vec2 and f32");
    CHECK(sourceError(wrap("x = 0.5 to #fff")) ==
          "'to' needs two numbers, two colors or two vec2 values, got f32 and color");
    CHECK(sourceError(wrap("x = #000 to #fff with 0.5")) == "'with' needs a distribution, got f32");
    const std::string infinite = "asset t { input i32 v = 0  size 1x1 output point p draw {\n  z = 0.0 p = {0.0, 0.0} to {1.0 / (z + float(1 - v)), 1.0} } }";
    for (const bool optimize : {false, true}) {
        CHECK_NOTHROW(generate(compileSource(infinite, optimize), 0, 0));
        CHECK_THROWS_AS(generate(compileSource(infinite, optimize), 0, 1), Trap);
    }
    const auto formatted = ips::formatProgram(ips::parse(
        "asset t { size 1x1 draw { c=#705845ff to #c8c8c8ff with beta(alpha=5.0,beta=2.0) q={1,2} to {3,4} } }"));
    CHECK(formatted.find("c = (#705845 to #c8c8c8 with beta(alpha = 5.0, beta = 2.0))") != std::string::npos);
    CHECK(formatted.find("q = ({1, 2} to {3, 4})") != std::string::npos);
}
