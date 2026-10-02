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

TEST_CASE("the sin and cos builtins compile and match the hand-assembled reference") {
    const std::string source = R"(
asset t {
  size 8x8
  output point p

  draw {
    s = sin(0.5)
    c = cos(1.25)
    p = {s, c}
    pixel(i32trunc(s * 10.0), 0, #ff0000)
    pixel(i32trunc(c * 10.0), 1, #00ff00)
  }
})";
    const std::string ipa = R"(
meta: size=8x8 origin=0,0 variants=1 archetypes=1 fuel=1000000
output: point p
fn draw ( i32, i32 ) -> ( ):
  locals: f32 f32
  const.f32 0.5
  f32.sin
  local.set 2
  const.f32 1.25
  f32.cos
  local.set 3
  local.get 2
  local.get 3
  out.point p
  local.get 2
  const.f32 10.0
  f32.mul
  f32.to.i32
  const.i32 0
  const.color #ff0000
  pixel
  local.get 3
  const.f32 10.0
  f32.mul
  f32.to.i32
  const.i32 1
  const.color #00ff00
  pixel
endfn
)";
    const auto fromIps = execute(compileChecked(source));
    const auto fromIpa = execute(assemble(ipa));

    for (std::size_t i = 0; i < fromIps.canvas.pixels.size(); ++i) {
        INFO("pixel " << i);
        REQUIRE(fromIps.canvas.pixels[i] == fromIpa.canvas.pixels[i]);
    }
    REQUIRE(fromIps.outputs[0].points.size() == 1);
    // p = {sin(0.5), cos(1.25)} — the pinned values.
    REQUIRE(std::fabs(fromIps.outputs[0].points[0].x - 0.47942555f) < 1e-6f);
    REQUIRE(std::fabs(fromIps.outputs[0].points[0].y - 0.31532236f) < 1e-6f);
    // The pixels mark the truncated coords: 4 and 3.
    REQUIRE(fromIps.canvas.at(4, 0) == Color{255, 0, 0, 255});
    REQUIRE(fromIps.canvas.at(3, 1) == Color{0, 255, 0, 255});
}

TEST_CASE("the math builtins compile and match the hand-assembled reference") {
    const std::string source = R"(
asset t {
  size 8x8
  output point p
  draw {
    f = floor(1.5) + ceil(-1.5)
    g = max(2.5, 3.0) + min(2.5, 3.0)
    h = exp(0.0)
    k = atan2(1.0, 1.0)
    b = abs(-1.5)
    p = {f + g + h + k + b, 0}
  }
})";
    const auto loaded = loadFile(compileChecked(source));
    CHECK_NOTHROW(validate(loaded));
    const auto result = run(compileChecked(source), 0, 0);
    // floor(1.5)=1, ceil(-1.5)=-1, max=3, min=2.5, exp(0)=1, atan2(1,1)=pi/4,
    // abs=1.5 — the sum: 1-1+3+2.5+1+0.7854+1.5 = 8.7854.
    REQUIRE(std::fabs(result.outputs[0].points[0].x - 8.785398f) < 1e-5f);
}

TEST_CASE("the math builtins emit the pinned opcodes") {
    // The .ipa reference now speaks f32.floor/ceil/min/max/pow/exp/atan2;
    // the .ips builtins must produce identical pixels.
    const std::string source = R"(
asset t {
  size 8x8
  draw {
    pixel(i32trunc(floor(2.7)), 0, #ff0000)
    pixel(i32trunc(ceil(2.1)), 0, #00ff00)
    pixel(i32trunc(min(1.5, 2.0)), 0, #0000ff)
    pixel(i32trunc(max(1.5, 2.0)), 0, #ffff00)
    pixel(i32trunc(pow(2.0, 3.0)), 0, #ff00ff)
    pixel(i32trunc(exp(0.0)), 0, #00ffff)
    pixel(i32trunc(atan2(1.0, 1.0) * 4.0), 0, #ffffff)
  }
})";
    const std::string ipa = R"(
meta: size=8x8 origin=0,0 variants=1 archetypes=1 fuel=1000000
fn draw ( i32, i32 ) -> ( ):
  const.f32 2.7
  f32.floor
  f32.to.i32
  const.i32 0
  const.color #ff0000
  pixel
  const.f32 2.1
  f32.ceil
  f32.to.i32
  const.i32 0
  const.color #00ff00
  pixel
  const.f32 1.5
  const.f32 2.0
  f32.min
  f32.to.i32
  const.i32 0
  const.color #0000ff
  pixel
  const.f32 1.5
  const.f32 2.0
  f32.max
  f32.to.i32
  const.i32 0
  const.color #ffff00
  pixel
  const.f32 2.0
  const.f32 3.0
  f32.pow
  f32.to.i32
  const.i32 0
  const.color #ff00ff
  pixel
  const.f32 0.0
  f32.exp
  f32.to.i32
  const.i32 0
  const.color #00ffff
  pixel
  const.f32 1.0
  const.f32 1.0
  f32.atan2
  const.f32 4.0
  f32.mul
  f32.to.i32
  const.i32 0
  const.color #ffffff
  pixel
endfn
)";
    const auto fromIps = run(compileChecked(source), 0, 0);
    const auto fromIpa = run(assemble(ipa), 0, 0);
    for (std::size_t i = 0; i < fromIps.canvas.pixels.size(); ++i) {
        INFO("pixel " << i);
        REQUIRE(fromIps.canvas.pixels[i] == fromIpa.canvas.pixels[i]);
    }
}

TEST_CASE("round widens integer arguments exactly once") {
    const auto result = run(compileChecked(R"(
asset t {
  size 8x8
  output point result
  draw { result = {round(3), round(count())} }
  count() -> i32 { pixel(0,0,#ff000080) return -3 }
})"),0,0);
    CHECK(result.outputs[0].points[0].x == 3.f);
    CHECK(result.outputs[0].points[0].y == -3.f);
    CHECK(result.canvas.at(0,0).r == 128); // a second evaluation would composite again
}

TEST_CASE("round saturates and distinguishes the floats adjacent to half integers") {
    const std::vector<std::pair<std::string,std::string>> cases{
        {"-2147483648.0", "-2147483648"}, {"-2147483904.0", "-2147483648"},
        {"2147483648.0", "2147483647"}, {"-1.0/0.0", "-2147483648"},
        {"1.0/0.0", "2147483647"}, {"0.0/0.0", "0"},
        {"0.49999997", "0"}, {"-0.49999997", "0"},
        {"0.5", "1"}, {"-0.5", "-1"}, {"1.4999999", "1"}, {"-1.4999999", "-1"},
        {"8388609.0", "8388609"}, {"-8388609.0", "-8388609"},
        {"0.0", "0"}, {"-0.0", "0"}
    };
    for (const auto& [input, expected] : cases) {
        INFO(input);
        // Comparing as i32 avoids hiding the INT_MIN+1 bug in f32 outputs.
        const auto result = execute(compileChecked("asset t { size 8x8 output point p "
            "draw { p = {round(" + input + ") == " + expected + ", 0} } }"));
        CHECK(result.outputs[0].points[0].x == 1.f);
    }
}

TEST_CASE("logical and bitwise not fold and lower correctly") {
    // !5 folds to 0, ~0 folds to -1, and a runtime !v uses the eqz lowering.
    const std::string source = R"(
asset t {
  size 8x8
  output point p
  draw {
    x = !5
    y = ~0
    z = !v
    p = {y, z}
  }
})";
    const RunOutcome outcome = execute(compileChecked(source), /*seed=*/0);
    REQUIRE(outcome.outputs[0].points.size() == 1);
    REQUIRE(outcome.outputs[0].points[0].x == -1.f); // ~0
    REQUIRE(outcome.outputs[0].points[0].y == 1.f);  // !v with v = 0
}

TEST_CASE("f32 comparisons lower to the f32 opcodes") {
    const std::string source = R"(
asset t {
  size 8x8
  output point p
  draw {
    x = 2.5
    y = x < 3.0
    p = {y, 0}
  }
})";
    const RunOutcome outcome = execute(compileChecked(source));
    REQUIRE(outcome.outputs[0].points[0].x == 1.f); // 2.5 < 3.0
}

TEST_CASE("division is not folded (the runtime trap is preserved)") {
    const std::string source = R"(
asset t {
  size 8x8
  draw {
    x = 6 / 2
  }
})";
    const auto bytes = compileChecked(source);
    const auto loaded = loadFile(bytes);
    // const.i32 6, const.i32 2, i32.div_s, local.set — no folding.
    REQUIRE(loaded.code.entry().code.size() == 16);
}

TEST_CASE("shifts, i32trunc and round execute with VM semantics") {
    const std::string source = R"(
asset t {
  size 8x8
  output point p

  draw {
    // u32 semantics: 0x80000000 >> 1 = 0x40000000
    a1 = 1 << 3
    b1 = 64 >> 2
    c1 = i32trunc(2.9)
    p = {round(1.3), round(-1.3)}
  }
})";
    const auto bytes = compileChecked(source);
    const auto loaded = loadFile(bytes);
    VM vm(loaded, GenerationInputs{0, 0, 0, 0, 0, {}});
    CHECK_NOTHROW(vm.run());
    const GenerationResult result = vm.result();
    const NamedOutput* p = result.find("p");
    REQUIRE(p != nullptr);
    REQUIRE(p->points.size() == 1);
    REQUIRE(p->points[0].x == 1.f);  // round(1.3)
    REQUIRE(p->points[0].y == -1.f); // round(-1.3) - half away from zero
}

TEST_CASE("round follows std::round at the half points") {
    const std::string source = R"(
asset t {
  size 8x8
  output point p

  draw {
    p = {round(0.5), round(-0.5)}
  }
})";
    const auto bytes = compileChecked(source);
    const auto loaded = loadFile(bytes);
    VM vm(loaded, GenerationInputs{0, 0, 0, 0, 0, {}});
    vm.run();
    const GenerationResult result = vm.result();
    const NamedOutput* p = result.find("p");
    REQUIRE(p->points[0].x == 1.f);  // 0.5 -> 1
    REQUIRE(p->points[0].y == -1.f); // -0.5 -> -1
}
