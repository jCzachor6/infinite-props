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

TEST_CASE("the SDF builtins render a soft-edged circle end to end") {
    const std::string source = R"(
asset t {
  size 16x16
  output point p

  cover(q: vec2) -> f32 {
    return fill(dist.circle(q, {8.0, 8.0}, 3.0), 1.0)
  }

  draw {
    p = {cover({8.0, 8.0}), cover({12.0, 8.0})}
    pixel(8, 8, #ff0000)
  }
})";
    const auto fromIps = run(compileChecked(source), 0, 0);
    // The centre is inside (1.0); {12,8} is one unit past the r=3 edge
    // with the w=1 band — the fill there is 0.
    REQUIRE(fromIps.outputs[0].points[0].x == 1.f);
    REQUIRE(fromIps.outputs[0].points[0].y == 0.f);
    REQUIRE(fromIps.canvas.at(8, 8) == Color{255, 0, 0, 255});

    const std::string ipa = R"(
meta: size=16x16 origin=0,0 variants=1 archetypes=1 fuel=1000000
output: point p
fn cover ( vec2 ) -> ( f32 ):
  local.get 0
  const.f32 8.0
  const.f32 8.0
  vec2.make
  const.f32 3.0
  dist.circle
  const.f32 1.0
  fill
  return
endfn
fn draw ( i32, i32 ) -> ( ):
  locals: f32 f32
  const.f32 8.0
  const.f32 8.0
  vec2.make
  call cover
  local.set 2
  const.f32 12.0
  const.f32 8.0
  vec2.make
  call cover
  local.set 3
  local.get 2
  local.get 3
  out.point p
  const.i32 8
  const.i32 8
  const.color #ff0000
  pixel
endfn
)";
    const auto fromIpa = run(assemble(ipa), 0, 0);
    REQUIRE(fromIpa.outputs[0].points[0].x == 1.f);
    REQUIRE(fromIpa.outputs[0].points[0].y == 0.f);
    for (std::size_t i = 0; i < fromIps.canvas.pixels.size(); ++i) {
        INFO("pixel " << i);
        REQUIRE(fromIps.canvas.pixels[i] == fromIpa.canvas.pixels[i]);
    }
}

TEST_CASE("the noise v2 builtins compile and match the hand-assembled reference") {
    const std::string source = R"(
asset t {
  size 8x8
  output point p

  draw {
    g = noise.grad(1.5, 2.5, 7)
    j = cell.jitter(3, 4, 7)
    r = noise.ridge(0.5, 0.5, 4, 7)
    p = {g + r, j.y}
  }
})";
    const auto fromIps = run(compileChecked(source), 0, 0);
    // g = 0.25 (0x3E800000), r = 0x3F444444 (~0.7667), j.y = 0x40820079
    // (~4.0626). The sum pins the same bits as the .ipa below.
    const auto bits = [](f32 value) { return std::bit_cast<u32>(value); };
    const auto fromBits = [](u32 value) { return std::bit_cast<f32>(value); };
    REQUIRE(bits(fromIps.outputs[0].points[0].x) ==
            bits(0.25f + fromBits(0x3F444444u)));
    REQUIRE(bits(fromIps.outputs[0].points[0].y) == 0x40820079u);

    const std::string ipa = R"(
meta: size=8x8 origin=0,0 variants=1 archetypes=1 fuel=1000000
output: point p
fn draw ( i32, i32 ) -> ( ):
  locals: f32 vec2 f32
  const.f32 1.5
  const.f32 2.5
  const.i32 7
  noise.grad
  local.set 2
  const.i32 3
  const.i32 4
  const.i32 7
  cell.jitter
  local.set 3
  const.f32 0.5
  const.f32 0.5
  const.i32 4
  const.i32 7
  noise.ridge
  local.set 4
  local.get 2
  local.get 4
  f32.add
  local.get 3
  vec2.y
  out.point p
endfn
)";
    const auto fromIpa = run(assemble(ipa), 0, 0);
    REQUIRE(bits(fromIpa.outputs[0].points[0].x) ==
            bits(fromIps.outputs[0].points[0].x));
    REQUIRE(bits(fromIpa.outputs[0].points[0].y) ==
            bits(fromIps.outputs[0].points[0].y));
}

TEST_CASE("the colour builtins compile and match the hand-assembled reference") {
    const std::string source = R"(
asset t {
  size 8x8
  draw {
    c = color.hsv(#ff0000, 0.16666667, 1.0, 1.0)
    c2 = color.mul(#ff8040, #4080ff)
    c3 = color.set.a(c2, 255)
    pixel(0, 0, c)
    pixel(1, 0, c2)
    pixel(2, 0, c3)
  }
})";
    const auto fromIps = run(compileChecked(source), 0, 0);
    REQUIRE(fromIps.canvas.at(0, 0) == Color{0xff, 0xff, 0x00, 0xff});
    REQUIRE(fromIps.canvas.at(1, 0) == Color{0x40, 0x40, 0x40, 0xff});
    // The set.a keeps the channels; the opaque alpha leaves the pixel
    // untouched (the semi-transparent case composites under §11.1).
    REQUIRE(fromIps.canvas.at(2, 0) == Color{0x40, 0x40, 0x40, 0xff});

    const std::string ipa = R"(
meta: size=8x8 origin=0,0 variants=1 archetypes=1 fuel=1000000
fn draw ( i32, i32 ) -> ( ):
  locals: color color color
  const.color #ff0000ff
  const.f32 0.16666667
  const.f32 1.0
  const.f32 1.0
  color.hsv
  local.set 2
  const.color #ff8040ff
  const.color #4080ffff
  color.mul
  local.set 3
  local.get 3
  const.i32 255
  color.set.a
  local.set 4
  const.i32 0
  const.i32 0
  local.get 2
  pixel
  const.i32 1
  const.i32 0
  local.get 3
  pixel
  const.i32 2
  const.i32 0
  local.get 4
  pixel
endfn
)";
    const auto fromIpa = run(assemble(ipa), 0, 0);
    for (std::size_t i = 0; i < fromIps.canvas.pixels.size(); ++i) {
        INFO("pixel " << i);
        REQUIRE(fromIps.canvas.pixels[i] == fromIpa.canvas.pixels[i]);
    }
}

TEST_CASE("the dither, fold and polar builtins compile and match the .ipa") {
    const std::string source = R"(
asset t {
  size 8x8
  output point p

  draw {
    f = fold({-3.0, 4.0}, 2)
    q = polar({3.0, 4.0})
    d = dither(0.7, 1, 0)
    p = {f.x + f.y, q.x + d}
  }
})";
    const auto fromIps = run(compileChecked(source), 0, 0);
    // fold({-3,4}, 2) -> the first-quadrant reflection at the same radius
    // (5): the {3, 4} exactly. q.x = 5; dither(0.7, 1, 0) = 1.
    REQUIRE(fromIps.outputs[0].points[0].x == 7.f);  // f.x + f.y = 3 + 4
    REQUIRE(fromIps.outputs[0].points[0].y == 6.f);  // q.x + d = 5 + 1

    const std::string ipa = R"(
meta: size=8x8 origin=0,0 variants=1 archetypes=1 fuel=1000000
output: point p
fn draw ( i32, i32 ) -> ( ):
  locals: vec2 vec2 f32
  const.f32 -3.0
  const.f32 4.0
  vec2.make
  const.i32 2
  vec2.fold
  local.set 2
  const.f32 3.0
  const.f32 4.0
  vec2.make
  vec2.polar
  local.set 3
  const.f32 0.7
  const.i32 1
  const.i32 0
  dither
  local.set 4
  local.get 2
  vec2.x
  local.get 2
  vec2.y
  f32.add
  local.get 3
  vec2.x
  local.get 4
  f32.add
  out.point p
endfn
)";
    const auto fromIpa = run(assemble(ipa), 0, 0);
    REQUIRE(fromIpa.outputs[0].points[0].x ==
            fromIps.outputs[0].points[0].x);
    REQUIRE(fromIpa.outputs[0].points[0].y ==
            fromIps.outputs[0].points[0].y);
}

TEST_CASE("quadratic and cubic Bezier queries and strokes survive assembly round trips") {
    const std::string source = R"(
asset t {
  size 8x8
  output point quadratic
  output point cubic
  output point bits
  draw {
    quadratic = {dist.bezier({3,0}, {0,0}, {1,0}, {2,0}),
                 dist.bezier({3,4}, {0,0}, {0,0}, {0,0})}
    cubic = {dist.bezier({4,0}, {0,0}, {1,0}, {2,0}, {3,0}),
             dist.bezier({1.5,3}, {0,0}, {0,2}, {3,2}, {3,0})}
    bits = {dist.bezier({-2.625,3.625}, {-4,4.75}, {-1,0.875}, {-5.875,-1.5}),
            dist.bezier({1.625,3.375}, {-1,-1.75}, {-2.875,3.875}, {3.5,3.375}, {4,-2})}
    curve({1,5}, {3,1}, {5,5}, 0.25, #0000ff)
    curve({1,5}, {1,1}, {5,1}, {5,5}, 0.25, #ff0000)
    curve({1,7}, {3,7}, {5,7}, 1, #00ff00)
  }
})";
    const auto bytes = compileChecked(source);
    const auto text = disassemble(bytes);
    REQUIRE(text.find("dist.bezier.cubic") != std::string::npos);
    REQUIRE(text.find("curve.cubic") != std::string::npos);
    const auto original = execute(bytes);
    const auto roundTrip = execute(assemble(text));
    REQUIRE(original.outputs[0].points[0].x == 1.f);
    REQUIRE(original.outputs[0].points[0].y == 5.f);
    REQUIRE(original.outputs[1].points[0].x == 1.f);
    REQUIRE(original.outputs[1].points[0].y == 1.5f);
    REQUIRE(std::bit_cast<u32>(original.outputs[2].points[0].x) == 0x3F17C01Bu);
    REQUIRE(std::bit_cast<u32>(original.outputs[2].points[0].y) == 0x3FA79662u);
    REQUIRE(original.canvas.at(3, 3) == Color{0, 0, 255, 255});
    REQUIRE(original.canvas.at(3, 2) == Color{255, 0, 0, 255});
    REQUIRE(original.canvas.at(3, 5) == Color{});
    REQUIRE(original.canvas.at(3, 7) == Color{0, 255, 0, 255});
    REQUIRE(original.canvas.pixels == roundTrip.canvas.pixels);
    for (std::size_t i = 0; i < original.outputs.size(); ++i) {
        REQUIRE(original.outputs[i].points[0].x == roundTrip.outputs[i].points[0].x);
        REQUIRE(original.outputs[i].points[0].y == roundTrip.outputs[i].points[0].y);
    }
}

TEST_CASE("Bezier overloads reject wrong counts and types in checker and compiler") {
    for (const std::string call : {
             "dist.bezier({0,0}, {1,1}, {2,0})",
             "dist.bezier({0,0}, {1,1}, {2,0}, {3,0}, {4,0}, {5,0})",
             "dist.bezier({0,0}, {1,1}, {2,0}, {3,0}, 4)",
             "curve({0,0}, {1,1}, {2,0}, #ffffff)",
             "curve({0,0}, {1,1}, {2,0}, {3,0}, 1, #ffffff, 7)",
             "curve({0,0}, {1,1}, {2,0}, #ffffff, #ffffff)",
             "curve({0,0}, {1,1}, {2,0}, {3,0}, 1, 2)",
             "curve({0,0}, {1,1}, {2,0}, width=1, colour=#ffffff)"}) {
        const auto program = ips::parse("asset t { size 8x8 draw { " + call + " } }");
        INFO(call);
        REQUIRE_THROWS_AS(ips::checkAsset(program, {}), ips::SourceError);
        REQUIRE_THROWS_AS(compileProgram(program, {}), ips::SourceError);
    }
}

TEST_CASE("list outputs, circle outputs and poly compile and run") {
    const std::string source = R"(
asset t {
  size 8x8
  output list point pts
  output circle c

  draw {
    pts = {1, 2}
    pts = {3, 4}
    c = {1, 2, 3}
    poly(#ffffff, 0, 0, 4, 0, 2, 2)
  }
})";
    const RunOutcome outcome = execute(compileChecked(source));
    const NamedOutput* pts = outcome.outputs.empty() ? nullptr : &outcome.outputs[0];
    REQUIRE(pts != nullptr);
    REQUIRE(pts->isList);
    REQUIRE(pts->points.size() == 2);
    const NamedOutput* circle = outcome.outputs.size() > 1 ? &outcome.outputs[1] : nullptr;
    REQUIRE(circle != nullptr);
    REQUIRE(circle->circles.size() == 1);
    // The poly painted the triangle interior.
    bool painted = false;
    for (const auto& pixel : outcome.canvas.pixels) {
        if (pixel.a != 0) {
            painted = true;
        }
    }
    REQUIRE(painted);
}

