#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <bit>
#include <map>
#include <string>
#include <vector>

#include "Compiler.hpp"
#include "iprop/VM.hpp"
#include "ips/Parser.hpp"
#include "ips/Semantics.hpp"
#include "ips/SourceError.hpp"
#include "ipa.hpp"
#include "ipdis.hpp"

#include "support/compiler_support.hpp"

using namespace iprop;
using namespace compiler_test;

TEST_CASE("palette indexing desugars to the picker ladder with pixel parity") {
    // The palette + indexing form ...
    const std::string palettes = R"(
asset t {
  size 8x8
  variants 8
  archetypes 8
  palette leafDark = [#365c38, #4c603e, #264a46, #704430, #54625c]
  draw(v: i32, a: i32) {
    c = leafDark[v]
    pixel(0, 0, c)
    d = leafDark[a]
    pixel(1, 0, d)
  }
})";
    // ... and the hand-written picker it replaces.
    const std::string picker = R"(
asset t {
  size 8x8
  variants 8
  archetypes 8
  draw(v: i32, a: i32) {
    c = pickLeafDark(v)
    pixel(0, 0, c)
    d = pickLeafDark(a)
    pixel(1, 0, d)
  }
  pickLeafDark(i: i32) -> color {
    c = #54625c
    if (i == 0) { c = #365c38 }
    if (i == 1) { c = #4c603e }
    if (i == 2) { c = #264a46 }
    if (i == 3) { c = #704430 }
    return c
  }
})";

    // Every (variant, archetype) combination renders identically.
    for (u32 variant = 0; variant < 8; ++variant) {
        for (u32 archetype = 0; archetype < 8; ++archetype) {
            INFO("variant " << variant << " archetype " << archetype);
            const auto fromPalettes = run(compileChecked(palettes), variant, archetype);
            const auto fromPicker = run(compileChecked(picker), variant, archetype);
            for (std::size_t i = 0; i < fromPalettes.canvas.pixels.size(); ++i) {
                REQUIRE(fromPalettes.canvas.pixels[i] ==
                        fromPicker.canvas.pixels[i]);
            }
        }
    }
}

TEST_CASE("palette index semantics: the last element is the default") {
    // Out-of-range and negative indices yield the last element, exactly
    // like the hand-written pickers.
    const std::string source = R"(
asset t {
  size 8x8
  palette p = [#111111, #222222, #333333]
  draw(v: i32, a: i32) {
    pixel(0, 0, p[0])
    pixel(1, 0, p[1])
    pixel(2, 0, p[2])
    pixel(3, 0, p[99])
    pixel(4, 0, p[0 - 1])
  }
})";
    const auto result = run(compileChecked(source), 0, 0);
    REQUIRE(result.canvas.at(0, 0) == Color{0x11, 0x11, 0x11, 0xff});
    REQUIRE(result.canvas.at(1, 0) == Color{0x22, 0x22, 0x22, 0xff});
    REQUIRE(result.canvas.at(2, 0) == Color{0x33, 0x33, 0x33, 0xff});
    REQUIRE(result.canvas.at(3, 0) == Color{0x33, 0x33, 0x33, 0xff});
    REQUIRE(result.canvas.at(4, 0) == Color{0x33, 0x33, 0x33, 0xff});
}

TEST_CASE("constant palette indices fold away the ladder") {
    const std::string folded = R"(
asset t {
  size 8x8
  palette p = [#111111, #222222, #333333]
  draw(v: i32, a: i32) {
    c = p[0]
  }
})";
    // The constant index needs no scratch locals (no ladder emitted): the
    // function has exactly one extra local (the result 'c').
    const auto loaded = loadFile(compileChecked(folded));
    REQUIRE(loaded.code.entry().locals.size() == 1);
}

TEST_CASE("dynamic palettes preserve indices and default for every element type") {
    const auto bytes = compileChecked(R"(
asset t {
  size 8x8
  variants 8
  palette ints = [1, 2, 0]
  palette flags = [true, false, true]
  palette floats = [1.25, 2.5, 3.75]
  palette colors = [#112233, #445566, #778899]
  output point numbers
  output point flag
  draw(v: i32, a: i32) {
    numbers = {ints[v - 1], floats[v - 1]}
    flag = {flags[v - 1], 0}
    pixel(0, 0, colors[v - 1])
  }
})");
    const i32 ints[] = {1, 2, 0};
    const i32 flags[] = {1, 0, 1};
    const f32 floats[] = {1.25f, 2.5f, 3.75f};
    const Color colors[] = {{0x11, 0x22, 0x33, 0xff},
                            {0x44, 0x55, 0x66, 0xff},
                            {0x77, 0x88, 0x99, 0xff}};
    for (u32 v = 0; v < 8; ++v) {
        INFO("variant " << v);
        const auto result = run(bytes, v, 0);
        const u32 k = v == 1 ? 0 : v == 2 ? 1 : 2;
        REQUIRE(result.outputs[0].points[0].x == ints[k]);
        REQUIRE(result.outputs[0].points[0].y == floats[k]);
        REQUIRE(result.outputs[1].points[0].x == flags[k]);
        REQUIRE(result.canvas.at(0, 0) == colors[k]);
    }
}

TEST_CASE("palette elements widen i32 indices and i32 palettes to f32 uses") {
    // An i32 palette feeding an f32 context widens (i32.to.f32), like
    // every other i32 expression.
    const std::string source = R"(
asset t {
  size 8x8
  palette w = [2, 4]
  output point p
  draw(v: i32, a: i32) {
    p = {w[v] + 0.5, 1}
  }
})";
    const auto loaded = loadFile(compileChecked(source));
    CHECK_NOTHROW(validate(loaded));
}

TEST_CASE("ramp interpolates palettes continuously") {
    const std::string source = R"(
asset t {
  size 8x8
  output point p
  palette stops = [#000000, #808080, #ffffff]
  palette heights = [0.0, 1.0, 4.0]

  draw(v: i32, a: i32) {
    c = ramp(stops, 0.5)
    c2 = ramp(stops, 1.0)
    h = ramp(heights, 0.75)
    p = {h, 0}
    pixel(0, 0, c)
    pixel(1, 0, c2)
  }
})";
    const auto fromIps = run(compileChecked(source), 0, 0);
    // t = 0.5 → p = 1, i = 1, f = 0 → stops[1].
    REQUIRE(fromIps.canvas.at(0, 0) == Color{0x80, 0x80, 0x80, 0xff});
    // t = 1 → the default stop (the last element).
    REQUIRE(fromIps.canvas.at(1, 0) == Color{0xff, 0xff, 0xff, 0xff});
    // t = 0.75 → p = 1.5, i = 1, f = 0.5 → lerp(1, 4, 0.5) = 2.5.
    REQUIRE(fromIps.outputs[0].points[0].x == 2.5f);

    // The historical hand-assembled linear .ipa reference for the f32 ramp.
    // New compiler strategies must preserve its result, not its fuel.
    const std::string ipa = R"(
meta: size=8x8 origin=0,0 variants=1 archetypes=1 fuel=1000000
output: point p
fn draw ( i32, i32 ) -> ( ):
  locals: i32 f32 f32
  const.f32 0.75
  const.f32 0.0
  f32.max
  const.f32 1.0
  f32.min
  const.f32 2.0
  f32.mul
  dup
  f32.to.i32
  local.set 2
  local.get 2
  i32.to.f32
  f32.sub
  local.set 3
  block
    const.i32 0
    local.get 2
    i32.eq
    if
      const.f32 0.0
      const.f32 1.0
      local.get 3
      lerp
      local.set 4
      br 1
    end
    const.i32 1
    local.get 2
    i32.eq
    if
      const.f32 1.0
      const.f32 4.0
      local.get 3
      lerp
      local.set 4
      br 1
    end
    const.f32 4.0
    local.set 4
  end
  local.get 4
  const.f32 0.0
  out.point p
endfn
)";
    const auto fromIpa = run(assemble(ipa), 0, 0);
    REQUIRE(fromIpa.outputs[0].points[0].x ==
            fromIps.outputs[0].points[0].x);
}

TEST_CASE("singleton ramps evaluate their numeric argument exactly once") {
    const std::string declarations = R"(asset t {
  size 8x8
  palette heights = [1.25]
  palette colors = [#123456]
  output point result
  output point following

  fraction() -> f32 {
    pixel(0, 0, #ff0000)
    return unit()
  }
  integer() -> i32 {
    pixel(1, 0, #00ff00)
    return index(7)
  }
)";
    const auto actualBytes = compileChecked(declarations + R"(
  draw(v: i32, a: i32) {
    height = ramp(heights, fraction())
    pixel(2, 0, ramp(colors, integer()))
    result = {height, unit()}
    following = {unit(), unit()}
  }
})");
    const auto referenceBytes = compileChecked(declarations + R"(
  draw(v: i32, a: i32) {
    discardedFraction = fraction()
    height = 1.25
    discardedInteger = integer()
    pixel(2, 0, #123456)
    result = {height, unit()}
    following = {unit(), unit()}
  }
})");
    for (const u32 seed : {0u, 1u, 7u, 42u, 0xFFFFFFFFu}) {
        CAPTURE(seed);
        const auto actual = execute(actualBytes, seed);
        const auto expected = execute(referenceBytes, seed);
        REQUIRE(actual.canvas.pixels == expected.canvas.pixels);
        REQUIRE(actual.canvas.at(0, 0) == Color{255, 0, 0, 255});
        REQUIRE(actual.canvas.at(1, 0) == Color{0, 255, 0, 255});
        REQUIRE(actual.canvas.at(2, 0) == Color{0x12, 0x34, 0x56, 255});
        REQUIRE(actual.outputs[0].points[0].x == 1.25f);
        for (std::size_t slot = 0; slot < expected.outputs.size(); ++slot) {
            REQUIRE(std::bit_cast<u32>(actual.outputs[slot].points[0].x) ==
                    std::bit_cast<u32>(expected.outputs[slot].points[0].x));
            REQUIRE(std::bit_cast<u32>(actual.outputs[slot].points[0].y) ==
                    std::bit_cast<u32>(expected.outputs[slot].points[0].y));
        }
    }
}

TEST_CASE("singleton ramp arguments retain runtime errors") {
    const auto bytes = compileChecked(R"(asset t {
  size 8x8
  palette heights = [1.25]
  draw(v: i32, a: i32) {
    height = ramp(heights, 1 / v)
  }
})");
    REQUIRE_THROWS_AS(execute(bytes), Trap);
}

TEST_CASE("palette resolution stays within each owner including shared part aliases") {
    const auto part = ips::parse(R"(asset child { size 8x8
      palette p = [1.25, 3.75]
      output point value
      draw(v: i32, a: i32) { value = {p[v], helper()} }
      helper() -> f32 { return ramp(p, 0.5) } })");
    const auto root = ips::parse(R"(import first from "child.ips"
      import second from "child.ips"
      asset root { size 8x8
        palette p = [#123456, #abcdef]
        draw(v: i32, a: i32) {
          first.draw(seed, v, a, at = {0, 0})
          second.draw(seed, v, a, at = {0, 0})
          pixel(0, 0, p[v])
          pixel(1, 0, ramp(p, 1.0))
        } })");
    ips::checkAsset(part, {});
    ips::checkAsset(root, {{"first", partInfoOf(part.asset)},
                           {"second", partInfoOf(part.asset)}});
    const auto result = execute(compileProgram(root, {{"first", &part}, {"second", &part}}));
    REQUIRE(result.outputs.size() == 2);
    for (const auto& output : result.outputs) {
        REQUIRE(output.points.size() == 1);
        CHECK(output.points[0].x == 1.25f);
        CHECK(output.points[0].y == 2.5f);
    }
    CHECK(result.canvas.pixels[0] == Color{0x12,0x34,0x56,255});
    CHECK(result.canvas.pixels[1] == Color{0xab,0xcd,0xef,255});
}

