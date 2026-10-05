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

TEST_CASE("the emitter safely rejects loop control without a loop") {
    for (const std::string control : {"break", "continue"}) {
        const auto program = ips::parse("asset t { size 8x8 "
            "draw {} helper() { " + control + " } }");
        CHECK_THROWS_AS(compileProgram(program, {}), ips::SourceError);
    }
}

TEST_CASE("break and continue lower to br on the loop labels") {
    // Sum the even i in 1..10, stopping early at i > 5: the contributes
    // 2 and 4 (continue skips the odds, break stops at 6) — the sum is 6.
    const std::string source = R"(
asset t {
  size 8x8
  output point p

  draw {
    s = 0
    i = 0
    while (i < 10) {
      i = i + 1
      if (i % 2 == 1) { continue }
      if (i > 5) { break }
      s = s + i
    }
    pixel(s, 0, #ff0000)
    p = {s, 0}
  }
})";
    const auto fromIps = run(compileChecked(source), 0, 0);
    REQUIRE(fromIps.outputs[0].points[0].x == 6);
    REQUIRE(fromIps.canvas.pixels[0] == Color{0, 0, 0, 0});
    REQUIRE(fromIps.canvas.pixels[6] == Color{255, 0, 0, 255});
    REQUIRE(fromIps.canvas.pixels[7] == Color{0, 0, 0, 0});

    // The hand-assembled reference: the same loop with explicit br depths
    // (br 0 = the loop label, br 1 = the enclosing block).
    const std::string ipa = R"(
meta: size=8x8 origin=0,0 variants=1 archetypes=1 fuel=1000000
output: point p
fn draw ( i32, i32 ) -> ( ):
  locals: i32 i32
  const.i32 0
  local.set 2
  const.i32 0
  local.set 3
  block
  loop
    local.get 3
    const.i32 1
    i32.add
    local.set 3
    local.get 3
    const.i32 2
    i32.rem_s
    const.i32 1
    i32.eq
    if
      br 1
    end
    local.get 3
    const.i32 5
    i32.gt_s
    if
      br 2
    end
    local.get 2
    local.get 3
    i32.add
    local.set 2
    br 0
  end
  end
  local.get 2
  const.i32 0
  const.color #ff0000
  pixel
  local.get 2
  i32.to.f32
  const.i32 0
  i32.to.f32
  out.point p
endfn
)";
    const auto fromIpa = run(assemble(ipa), 0, 0);
    REQUIRE(fromIpa.outputs[0].points[0].x == 6);
    for (std::size_t i = 0; i < fromIps.canvas.pixels.size(); ++i) {
        INFO("pixel " << i);
        REQUIRE(fromIps.canvas.pixels[i] == fromIpa.canvas.pixels[i]);
    }
}

TEST_CASE("break and continue work inside nested blocks and loops") {
    // A break inside an if inside the loop exits the loop; a continue
    // inside a nested while still targets that nested while (not the
    // outer one).
    const std::string source = R"(
asset t {
  size 8x8
  output point p

  draw {
    total = 0
    i = 0
    while (i < 4) {
      i = i + 1
      j = 0
      while (j < 4) {
        j = j + 1
        if (j == i) { continue }
        total = total + 1
      }
      if (total > 10) { break }
    }
    p = {total, 0}
  }
})";
    const auto result = run(compileChecked(source), 0, 0);
    // The inner while skips j == i (one skip per inner run — 4 total over
    // all runs): the full nested count would be 4*4 = 16, minus the skips
    // — but the outer break at total > 10 stops it. After i = 1: 3; i = 2:
    // 3 more (6); i = 3: 3 more (9); i = 4: the inner adds 3 (12) — then
    // the outer break fires — total = 12.
    REQUIRE(result.outputs[0].points[0].x == 12);
}

TEST_CASE("for loops lower to block/loop/block with the increment on continue") {
    // s sums i in 0..5 except 2 (continue jumps past the body to the
    // increment): 0+1+3+4 = 8. This pins the three-label lowering — a
    // naive while-desugar would skip the increment on continue.
    const std::string source = R"(
asset t {
  size 8x8
  output point p

  draw {
    s = 0
    for i in 0..5 {
      if (i == 2) { continue }
      s = s + i
    }
    p = {s, 0}
  }
})";
    const auto fromIps = run(compileChecked(source), 0, 0);
    REQUIRE(fromIps.outputs[0].points[0].x == 8);

    const std::string ipa = R"(
meta: size=8x8 origin=0,0 variants=1 archetypes=1 fuel=1000000
output: point p
fn draw ( i32, i32 ) -> ( ):
  locals: i32 i32 i32
  const.i32 0
  local.set 2
  const.i32 0
  local.set 3
  const.i32 5
  local.set 4
  block
  loop
    local.get 3
    local.get 4
    i32.lt_s
    const.i32 0
    i32.eq
    br_if 1
    block
      local.get 3
      const.i32 2
      i32.eq
      if
        br 1
      end
      local.get 2
      local.get 3
      i32.add
      local.set 2
    end
    local.get 3
    const.i32 1
    i32.add
    local.set 3
    br 0
  end
  end
  local.get 2
  i32.to.f32
  const.i32 0
  i32.to.f32
  out.point p
endfn
)";
    const auto fromIpa = run(assemble(ipa), 0, 0);
    REQUIRE(fromIpa.outputs[0].points[0].x == 8);
    for (std::size_t i = 0; i < fromIps.canvas.pixels.size(); ++i) {
        INFO("pixel " << i);
        REQUIRE(fromIps.canvas.pixels[i] == fromIpa.canvas.pixels[i]);
    }
}

TEST_CASE("for loop bounds are evaluated exactly once") {
    // The bound index(3) draws from the stream exactly once; the while
    // reference below consumes the same draws in the same order, so the
    // loop counts and the trailing draw must agree. A bound re-evaluated
    // per iteration would advance the stream and diverge.
    const std::string forSource = R"(
asset t {
  size 8x8
  output point p

  draw {
    count = 0
    for i in 0..index(3) {
      count = count + 1
    }
    after = index(3)
    p = {count, after}
  }
})";
    const std::string whileSource = R"(
asset t {
  size 8x8
  output point p

  draw {
    count = 0
    bound = index(3)
    i = 0
    while (i < bound) {
      count = count + 1
      i = i + 1
    }
    after = index(3)
    p = {count, after}
  }
})";
    const auto forRun = run(compileChecked(forSource), 0, 0);
    const auto whileRun = run(compileChecked(whileSource), 0, 0);
    REQUIRE(forRun.outputs[0].points[0].x == whileRun.outputs[0].points[0].x);
    REQUIRE(forRun.outputs[0].points[0].y == whileRun.outputs[0].points[0].y);
}

TEST_CASE("&& and || short-circuit: side effects are skipped") {
    const std::string source = R"(
asset t {
  size 8x8
  draw {
    c = 0 && mark()
    d = 1 && mark2()
  }
  mark() -> i32 {
    pixel(0, 0, #ff0000)
    return 1
  }
  mark2() -> i32 {
    pixel(1, 1, #00ff00)
    return 1
  }
})";
    const RunOutcome outcome = execute(compileChecked(source));
    // mark() must NOT have run (0 && ...); mark2() must have.
    REQUIRE(outcome.canvas.at(0, 0).a == 0);
    REQUIRE(outcome.canvas.at(1, 1) == Color{0, 255, 0, 255});
}

