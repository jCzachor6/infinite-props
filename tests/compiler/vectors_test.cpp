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

TEST_CASE("typed temporary locals keep stable slots in every emission order") {
    const std::vector<std::string> statements = {
        "c = colors[v] ", "x = round(range(1.25, 1.25)) ",
        "flag = v == 0 || v == 1 && v < 2 "};
    for (const auto& order : std::vector<std::vector<std::size_t>>{
             {0, 1, 2}, {0, 2, 1}, {1, 0, 2}, {1, 2, 0}, {2, 0, 1}, {2, 1, 0}}) {
        const std::string body = statements[order[0]] + statements[order[1]] +
                                 statements[order[2]];
        const std::string source = "asset t { size 8x8 variants 3 "
            "palette colors = [#112233, #445566] "
            "draw { " + body + "pixel(x, flag, c) "
            "pixel(3, 0, helper(v)) } "
            "helper(v: i32) -> color { " + body + "return c } "
            "constant() -> i32 { return 1 } }";
        INFO(body);
        const auto bytes = compileChecked(source);
        const auto loaded = loadFile(bytes);
        REQUIRE(loaded.code.functions.back().locals.empty());
        for (u32 v = 0; v < 3; ++v) {
            const auto result = run(bytes, v, 0);
            const Color expected = v == 0 ? Color{0x11, 0x22, 0x33, 0xff}
                                           : Color{0x44, 0x55, 0x66, 0xff};
            REQUIRE(result.canvas.at(1, v < 2 ? 1 : 0) == expected);
            REQUIRE(result.canvas.at(3, 0) == expected);
        }
    }
}

TEST_CASE("vec2 locals, operators, swizzles and helpers compile and run") {
    const std::string source = R"(
asset t {
  size 64x64
  output point p

  helper(a: vec2, s: f32) -> vec2 {
    return a * s + {1.0, 2.0}
  }

  draw {
    u = {3.0, 4.0}
    w = u + u
    d = w.x * w.y
    q = -u
    r = helper(u, 2.0)
    p = r
    pixel(i32trunc(d), 0, #ff0000)
  }
})";
    const auto fromIps = run(compileChecked(source), 0, 0);
    // u = {3, 4}; w = {6, 8}; d = 48; r = {3*2+1, 4*2+2} = {7, 10}.
    REQUIRE(fromIps.outputs[0].points[0].x == 7.f);
    REQUIRE(fromIps.outputs[0].points[0].y == 10.f);
    REQUIRE(fromIps.canvas.pixels[48] == Color{255, 0, 0, 255});

    // The hand-assembled .ipa reference.
    const std::string ipa = R"(
meta: size=64x64 origin=0,0 variants=1 archetypes=1 fuel=1000000
output: point p
fn draw ( i32, i32 ) -> ( ):
  locals: vec2 vec2 f32 vec2 vec2
  const.f32 3.0
  const.f32 4.0
  vec2.make
  local.set 2
  local.get 2
  local.get 2
  vec2.add
  local.set 3
  local.get 3
  vec2.x
  local.get 3
  vec2.y
  f32.mul
  local.set 4
  local.get 2
  vec2.neg
  local.set 5
  local.get 2
  const.f32 2.0
  call helper
  local.set 6
  local.get 6
  vec2.x
  local.get 6
  vec2.y
  out.point p
  local.get 4
  f32.to.i32
  const.i32 0
  const.color #ff0000
  pixel
endfn
fn helper ( vec2, f32 ) -> ( vec2 ):
  local.get 0
  local.get 1
  vec2.mul
  const.f32 1.0
  const.f32 2.0
  vec2.make
  vec2.add
  return
endfn
)";
    const auto fromIpa = run(assemble(ipa), 0, 0);
    REQUIRE(fromIpa.outputs[0].points[0].x == 7.f);
    REQUIRE(fromIpa.outputs[0].points[0].y == 10.f);
    for (std::size_t i = 0; i < fromIps.canvas.pixels.size(); ++i) {
        INFO("pixel " << i);
        REQUIRE(fromIps.canvas.pixels[i] == fromIpa.canvas.pixels[i]);
    }
}

TEST_CASE("vec2 locals are zeroed in root, helper and part frames") {
    SECTION("root and helper") {
        const auto bytes = compileChecked(R"(
asset t {
  size 8x8
  variants 2
  output point rootPoint
  output point helperPoint
  helper(flag: i32) -> vec2 {
    if (flag) { q = {3.0, 4.0} }
    return q
  }
  draw {
    if (v) { q = {1.0, 2.0} }
    rootPoint = q
    helperPoint = helper(v)
  }
})");
        for (const u32 variant : {0u, 1u}) {
            INFO("variant " << variant);
            const auto result = run(bytes, variant, 0);
            REQUIRE(result.outputs[0].points.size() == 1);
            REQUIRE(result.outputs[1].points.size() == 1);
            REQUIRE(result.outputs[0].points[0].x == (variant ? 1.f : 0.f));
            REQUIRE(result.outputs[0].points[0].y == (variant ? 2.f : 0.f));
            REQUIRE(result.outputs[1].points[0].x == (variant ? 3.f : 0.f));
            REQUIRE(result.outputs[1].points[0].y == (variant ? 4.f : 0.f));
        }
    }
    SECTION("part") {
        const auto part = ips::parse(R"(asset child {
          size 8x8 variants 2 output point top
          draw {
            if (v) { q = {5.0, 6.0} }
            top = q
          }
        })");
        const auto root = ips::parse(R"(import child from "child.ips"
          asset t { size 8x8 variants 2 output point p
            draw {
              child.draw(seed, v, a, at = {0, 0})
              p = child.outputs.top
            }
          })");
        ips::checkAsset(part, {});
        ips::checkAsset(root, {{"child", partInfoOf(part.asset)}});
        const auto bytes = compileProgram(root, {{"child", &part}});
        for (const u32 variant : {0u, 1u}) {
            INFO("variant " << variant);
            const auto result = run(bytes, variant, 0);
            REQUIRE(result.outputs[0].points.size() == 1);
            REQUIRE(result.outputs[0].points[0].x == (variant ? 5.f : 0.f));
            REQUIRE(result.outputs[0].points[0].y == (variant ? 6.f : 0.f));
        }
    }
}

TEST_CASE("vec2 operators compile every supported vector and scalar combination") {
    const auto result = execute(compileChecked(R"(
asset t {
  size 8x8
  output point sum
  output point difference
  output point scaledRight
  output point scaledLeft
  output point product
  output point dividedScalar
  output point dividedVector
  draw {
    q = {4, 8}
    sum = q + {1, 2}
    difference = q - {1, 2}
    scaledRight = q * 2
    scaledLeft = 2 * q
    product = q * {2, 3}
    dividedScalar = q / 2
    dividedVector = q / {2, 4}
  }
})"));
    const std::vector<Vec2> expected = {
        {5.f, 10.f}, {3.f, 6.f}, {8.f, 16.f}, {8.f, 16.f},
        {8.f, 24.f}, {2.f, 4.f}, {2.f, 2.f}};
    REQUIRE(result.outputs.size() == expected.size());
    for (std::size_t i = 0; i < expected.size(); ++i) {
        INFO("output " << result.outputs[i].name);
        REQUIRE(result.outputs[i].points.size() == 1);
        REQUIRE(result.outputs[i].points[0].x == expected[i].x);
        REQUIRE(result.outputs[i].points[0].y == expected[i].y);
    }
}

TEST_CASE("sequential scalar-vector products reuse locals without limiting source length") {
    for (const bool scalarLeft : {false, true}) {
        std::string source = "asset t { size 1x1 output point p "
                             "draw { q={1,2} ";
        for (unsigned i = 0; i < 2048; ++i) {
            source += scalarLeft ? "q=1.0*q\n" : "q=q*1.0\n";
        }
        source += "p=q } }";
        const auto bytes = compileChecked(source);
        const auto loaded = loadFile(bytes);
        REQUIRE(loaded.code.entry().locals.size() <= 2);
        const auto result = execute(bytes);
        REQUIRE(result.outputs[0].points.size() == 1);
        CHECK(result.outputs[0].points[0].x == 1.f);
        CHECK(result.outputs[0].points[0].y == 2.f);
    }
}

TEST_CASE("scalar-vector products preserve left-to-right RNG evaluation") {
    struct Fixture { std::string expression; std::string sequential; };
    const std::vector<Fixture> fixtures = {
        {"unit() * {unit(), unit()}",
         "s = unit() q = {unit(), unit()} p = q * s"},
        {"index(7) * {unit(), unit()}",
         "s = index(7) q = {unit(), unit()} p = q * s"},
        {"unit() * (unit() * {unit(), unit()})",
         "s = unit() t = unit() q = {unit(), unit()} p = (q * t) * s"},
        {"unit() * {round(unit() * 5), unit()}",
         "s = unit() q = {round(unit() * 5), unit()} p = q * s"}};
    const auto source = [](const std::string& body) {
        return "asset t { size 8x8 output point p output point following "
               "draw { " + body +
               " following = {unit(), unit()} } }";
    };
    for (const auto& fixture : fixtures) {
        INFO(fixture.expression);
        const auto inlineBytes = compileChecked(source("p = " + fixture.expression));
        const auto sequentialBytes = compileChecked(source(fixture.sequential));
        for (const u32 seed : {0u, 1u, 7u, 42u, 0xFFFFFFFFu}) {
            INFO("seed " << seed);
            const auto actual = execute(inlineBytes, seed);
            const auto expected = execute(sequentialBytes, seed);
            REQUIRE(actual.outputs.size() == expected.outputs.size());
            for (std::size_t i = 0; i < expected.outputs.size(); ++i) {
                REQUIRE(actual.outputs[i].points.size() == 1);
                REQUIRE(expected.outputs[i].points.size() == 1);
                REQUIRE(actual.outputs[i].points[0].x == expected.outputs[i].points[0].x);
                REQUIRE(actual.outputs[i].points[0].y == expected.outputs[i].points[0].y);
            }
        }
    }
}

TEST_CASE("part point outputs expose vec2 components without a temporary") {
    const auto part = ips::parse(R"(asset child {
      size 8x8 output point top output point x output point y
      draw { top = {3, 4} x = {5, 6} y = {7, 8} }
    })");
    const auto root = ips::parse(R"(import child from "child.ips"
      asset t { size 8x8
        output point components
        output point named
        output point translated
        output point bare
        draw {
          child.draw(seed, v, a, at = {0, 0})
          components = {child.outputs.top.x, child.outputs.top.y}
          named = {child.outputs.x.x, child.outputs.y.y}
          translated = {(child.outputs.top + {1, 2}).x,
                        (child.outputs.top + {1, 2}).y}
          bare = child.outputs.x
        }
      })");
    ips::checkAsset(part, {});
    ips::checkAsset(root, {{"child", partInfoOf(part.asset)}});
    const auto result = execute(compileProgram(root, {{"child", &part}}));
    const std::vector<Vec2> expected = {
        {3.f, 4.f}, {5.f, 8.f}, {4.f, 6.f}, {5.f, 6.f}};
    for (std::size_t i = 0; i < expected.size(); ++i) {
        INFO("output " << result.outputs[i].name);
        REQUIRE(result.outputs[i].points.size() == 1);
        REQUIRE(result.outputs[i].points[0].x == expected[i].x);
        REQUIRE(result.outputs[i].points[0].y == expected[i].y);
    }
}

TEST_CASE("vec2 source builtins use counterclockwise asset angles") {
    const auto result = run(compileChecked(R"(
asset t {
  size 8x8
  output point metrics
  output point positive
  output point negative
  output point angle
  output point integerAngle
  draw {
    metrics = {vec2.len({3,4}), vec2.dot({2,3},{4,5})}
    positive = vec2.rot({2,3}, 1.57079632679)
    negative = vec2.rot({2,3}, -1.57079632679)
    angle = polar({0,-1})
    integerAngle = vec2.rot({2,3}, 0)
  }
})"),0,0);
    CHECK(result.outputs[0].points[0].x == 5.f);
    CHECK(result.outputs[0].points[0].y == 23.f);
    CHECK(std::fabs(result.outputs[1].points[0].x - 3.f) < 1e-5f);
    CHECK(std::fabs(result.outputs[1].points[0].y + 2.f) < 1e-5f);
    CHECK(std::fabs(result.outputs[2].points[0].x + 3.f) < 1e-5f);
    CHECK(std::fabs(result.outputs[2].points[0].y - 2.f) < 1e-5f);
    CHECK(result.outputs[3].points[0].x == 1.f);
    CHECK(std::fabs(result.outputs[3].points[0].y - 1.57079632679f) < 1e-5f);
    CHECK(result.outputs[4].points[0].x == 2.f);
    CHECK(result.outputs[4].points[0].y == 3.f);
}

