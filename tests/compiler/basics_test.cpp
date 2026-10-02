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

namespace {
const std::string kRockSource = R"(
asset rock {
  size 192x192
  origin 96,170
  variants 5
  archetypes 4
  input  color palette = #808d74
  output rect collider

  draw {
    w = range(21, 31)
    h = range(35, 49)
    collider = {96 - 20, 170 - 23, 40, 23}
    rect(-w * 0.10, 0, w * 0.87, h, palette)
    if (index(3) == 0) {
      rect(-w * 0.67, 2, w * 0.39, h * 0.43, shade(palette, -4))
    }
  }
})";

// A deterministic fixture with no RNG calls: arithmetic, canvas ops, an if,
// a while loop, output writes and an input uniform.
const std::string kFixture = R"(
asset t {
  size 8x8
  input color base = #3355aa
  output point p

  draw {
    x = 2 + 3 * 4
    w = x - 6
    rect(0, 0, w, w, base)
    p = {x, w}
    if (x == 14) {
      pixel(0, 0, shade(base, 10))
    }
    i = 0
    while (i < 2) {
      pixel(i, 1, #ffffff)
      i = i + 1
    }
  }
})";

// The hand-assembled .ipa equivalent of kFixture.
const std::string kFixtureIpa = R"(
meta: size=8x8 origin=0,0 variants=1 archetypes=1 fuel=1000000
input: color base = #3355aa
output: point p
fn draw ( i32, i32, color ) -> ( ):
  locals: i32 i32 i32
  const.i32 14
  local.set 3
  local.get 3
  const.i32 6
  i32.sub
  local.set 4
  const.i32 0
  i32.to.f32
  const.i32 0
  i32.to.f32
  local.get 4
  i32.to.f32
  local.get 4
  i32.to.f32
  local.get 2
  rect
  local.get 3
  i32.to.f32
  local.get 4
  i32.to.f32
  out.point p
  local.get 3
  const.i32 14
  i32.eq
  if
  const.i32 0
  const.i32 0
  local.get 2
  const.i32 10
  color.shade
  pixel
  end
  const.i32 0
  local.set 5
  block
  loop
  local.get 5
  const.i32 2
  i32.lt_s
  const.i32 0
  i32.eq
  br_if 1
  local.get 5
  const.i32 1
  const.color #ffffff
  pixel
  local.get 5
  const.i32 1
  i32.add
  local.set 5
  br 0
  end
  end
endfn
)";

} // namespace

TEST_CASE("compiled rock validates and runs") {
    const auto bytes = compileChecked(kRockSource);
    const auto loaded = loadFile(bytes);
    CHECK_NOTHROW(validate(loaded));

    // Executes without trapping (RNG draws differ per seed, but the canvas
    // must be produced).
    VM vm(loaded, GenerationInputs{7, 0, 0, 0, 0, {}});
    CHECK_NOTHROW(vm.run());
    REQUIRE(vm.canvas().width == 192);
    REQUIRE(vm.canvas().height == 192);

    // The collider output is a rect.
    const auto result = vm.result();
    const NamedOutput* collider = result.find("collider");
    REQUIRE(collider != nullptr);
    REQUIRE(collider->rects.size() == 1);
}

TEST_CASE("compiled output executes identically to the hand-assembled fixture") {
    const auto fromIps = execute(compileChecked(kFixture));
    const auto fromIpa = execute(assemble(kFixtureIpa));

    REQUIRE(fromIps.canvas.width == fromIpa.canvas.width);
    REQUIRE(fromIps.canvas.height == fromIpa.canvas.height);
    for (std::size_t i = 0; i < fromIps.canvas.pixels.size(); ++i) {
        INFO("pixel " << i);
        REQUIRE(fromIps.canvas.pixels[i] == fromIpa.canvas.pixels[i]);
    }

    REQUIRE(fromIps.outputs.size() == fromIpa.outputs.size());
    REQUIRE(fromIps.outputs[0].name == fromIpa.outputs[0].name);
    REQUIRE(fromIps.outputs[0].points.size() == 1);
    REQUIRE(fromIps.outputs[0].points[0].x == 14.f); // x = 2 + 3*4
    REQUIRE(fromIps.outputs[0].points[0].y == 8.f);  // w = 14 - 6
    REQUIRE(fromIps.outputs[0].points[0].x == fromIpa.outputs[0].points[0].x);
    REQUIRE(fromIps.outputs[0].points[0].y == fromIpa.outputs[0].points[0].y);
}

TEST_CASE("two compilations are byte-identical") {
    REQUIRE(compileChecked(kRockSource) == compileChecked(kRockSource));
    REQUIRE(compileChecked(kFixture) == compileChecked(kFixture));
}

TEST_CASE("constant folding collapses literal arithmetic") {
    const std::string source = R"(
asset t {
  size 8x8
  draw {
    x = 2 + 3 * 4
    y = -5 + 2
  }
})";
    const auto bytes = compileChecked(source);
    const auto loaded = loadFile(bytes);
    // Two folded constants, two local sets: 4 instructions = 16 bytes.
    REQUIRE(loaded.code.entry().code.size() == 16);
    // x and y are the only extra locals.
    REQUIRE(loaded.code.entry().locals.size() == 2);
}

TEST_CASE("compiler-only error cases carry locations") {
    const auto expectError = [](const std::string& source, const char* part) {
        const ips::Program program = ips::parse(source);
        ips::checkAsset(program, {}); // must pass the checker first
        bool threw = false;
        try {
            (void)compileProgram(program, {});
        } catch (const ips::SourceError& error) {
            threw = true;
            INFO(error.message);
            REQUIRE(std::string(error.message).find(part) != std::string::npos);
        }
        REQUIRE(threw);
    };

    // Origin must be a constant.
    expectError("asset t { size 8x8 origin v,0 draw { } }",
                "origin must be a constant");

    // The v0.1 format has no i32 uniform type.
    expectError("asset t { size 8x8 input i32 x = 1 draw { } }",
                "input i32 is not supported");
}

