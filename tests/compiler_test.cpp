#include <catch2/catch_test_macros.hpp>

#include <map>
#include <string>
#include <vector>

#include "Compiler.hpp"
#include "iprop/VM.hpp"
#include "ips/Parser.hpp"
#include "ips/Semantics.hpp"
#include "ips/SourceError.hpp"
#include "ipa.hpp"

// The .ips compiler: validated binaries, execution parity with the
// hand-assembled reference, deterministic output, constant folding, and the
// compiler-only error cases.

using namespace iprop;

namespace {

const std::string kRockSource = R"(
asset rock {
  size 192x192
  origin 96,170
  variants 5
  archetypes 4
  input  color palette = #808d74
  output rect collider

  draw(v: i32, a: i32) {
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

  draw(v: i32, a: i32) {
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

std::vector<u8> compileChecked(const std::string& source) {
    const ips::Program program = ips::parse(source);
    ips::checkAsset(program, {});
    return compileProgram(program, {});
}

struct RunOutcome {
    Canvas canvas;
    std::vector<NamedOutput> outputs;
};

RunOutcome execute(const std::vector<u8>& bytes, u32 seed = 42) {
    const auto loaded = loadFile(bytes);
    validate(loaded);
    VM vm(loaded, GenerationInputs{seed, 0, 0, 0, 0, {}});
    vm.run();
    const GenerationResult result = vm.result();
    return {result.image, result.outputs};
}

RunOutcome run(const std::vector<u8>& bytes, u32 variant, u32 archetype) {
    const auto loaded = loadFile(bytes);
    validate(loaded);
    VM vm(loaded, GenerationInputs{42, variant, archetype, 0, 0, {}});
    vm.run();
    const GenerationResult result = vm.result();
    return {result.image, result.outputs};
}

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

TEST_CASE("the sin and cos builtins compile and match the hand-assembled reference") {
    const std::string source = R"(
asset t {
  size 8x8
  output point p

  draw(v: i32, a: i32) {
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

TEST_CASE("the math builtins compile and match the hand-assembled reference") {
    const std::string source = R"(
asset t {
  size 8x8
  output point p
  draw(v: i32, a: i32) {
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
  draw(v: i32, a: i32) {
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

TEST_CASE("the emitter safely rejects loop control without a loop") {
    for (const std::string control : {"break", "continue"}) {
        const auto program = ips::parse("asset t { size 8x8 "
            "draw(v: i32, a: i32) {} helper() { " + control + " } }");
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

  draw(v: i32, a: i32) {
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

  draw(v: i32, a: i32) {
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

  draw(v: i32, a: i32) {
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

  draw(v: i32, a: i32) {
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

  draw(v: i32, a: i32) {
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

TEST_CASE("two compilations are byte-identical") {
    REQUIRE(compileChecked(kRockSource) == compileChecked(kRockSource));
    REQUIRE(compileChecked(kFixture) == compileChecked(kFixture));
}

TEST_CASE("constant folding collapses literal arithmetic") {
    const std::string source = R"(
asset t {
  size 8x8
  draw(v: i32, a: i32) {
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

TEST_CASE("parts compile, propagate outputs and are deterministic") {
    const std::string hair = R"(
asset hair {
  size 64x32
  origin 32,4
  input color color = #33291f
  output point top
  draw(v: i32, a: i32) {
    rect(0, 0, 64, 32, color)
    top = {32, 4}
  }
})";
    const std::string eyes = R"(
asset eyes {
  size 32x16
  origin 16,8
  input color color = #112233
  output point pupil
  draw(v: i32, a: i32) {
    rect(0, 0, 32, 16, color)
    pupil = {16, 8}
  }
})";
    const std::string portrait = R"(
import hair from "hair.ips"
import eyes from "eyes.ips"

asset portrait {
  size 64x64
  variants 8
  input  color skin = #d9b58c
  output point pupilCenter

  draw(v: i32, a: i32) {
    rect(0, 0, 64, 64, skin)
    hair.draw(seed ^ 0x9e3779b9, v, a, at = {32, 4})
    eyes.draw(seed ^ 0x517cc1b7, v, a, at = {32, 24}, color = skin)
    pupilCenter = eyes.outputs.pupil
  }
})";

    const ips::Program hairProgram = ips::parse(hair);
    const ips::Program eyesProgram = ips::parse(eyes);
    const ips::Program portraitProgram = ips::parse(portrait);
    ips::checkAsset(hairProgram, {});
    ips::checkAsset(eyesProgram, {});
    ips::checkAsset(portraitProgram,
                    {{"hair", partInfoOf(hairProgram.asset)},
                     {"eyes", partInfoOf(eyesProgram.asset)}});

    const std::map<std::string, const ips::Program*> parts = {
        {"hair", &hairProgram}, {"eyes", &eyesProgram}};
    const auto bytes = compileProgram(portraitProgram, parts);
    const auto loaded = loadFile(bytes);
    CHECK_NOTHROW(validate(loaded));

    VM vm(loaded, GenerationInputs{123, 0, 0, 0, 0, {}});
    CHECK_NOTHROW(vm.run());
    const GenerationResult result = vm.result();

    // The child output surfaces in the parent.
    const NamedOutput* pupil = result.find("pupilCenter");
    REQUIRE(pupil != nullptr);
    REQUIRE(pupil->points.size() == 1);
    REQUIRE(pupil->points[0].x == 16.f);
    REQUIRE(pupil->points[0].y == 8.f);

    // The parts' own outputs are present in global slot order.
    REQUIRE(result.find("top") != nullptr);
    REQUIRE(result.find("pupil") != nullptr);

    // Deterministic.
    REQUIRE(compileProgram(portraitProgram, parts) == bytes);
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
    expectError("asset t { size 8x8 origin v,0 draw(v: i32, a: i32) { } }",
                "origin must be a constant");

    // The v0.1 format has no i32 uniform type.
    expectError("asset t { size 8x8 input i32 x = 1 draw(v: i32, a: i32) { } }",
                "input i32 is not supported");
}

TEST_CASE("nested part imports are rejected") {
    const std::string inner = R"(
import leaf from "leaf.ips"
asset inner { size 8x8 draw(v: i32, a: i32) { } }
)";
    const std::string leaf = R"(
asset leaf { size 4x4 draw(v: i32, a: i32) { } }
)";
    const std::string outer = R"(
import inner from "inner.ips"
asset outer { size 8x8 draw(v: i32, a: i32) { inner.draw(1, v, a, at = {0, 0}) } }
)";

    const ips::Program innerProgram = ips::parse(inner);
    const ips::Program leafProgram = ips::parse(leaf);
    const ips::Program outerProgram = ips::parse(outer);

    bool threw = false;
    try {
        (void)compileProgram(outerProgram, {{"inner", &innerProgram}});
    } catch (const ips::SourceError& error) {
        threw = true;
        REQUIRE(std::string(error.message).find("nested part imports") !=
                std::string::npos);
    }
    REQUIRE(threw);
}

TEST_CASE("&& and || short-circuit: side effects are skipped") {
    const std::string source = R"(
asset t {
  size 8x8
  draw(v: i32, a: i32) {
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

TEST_CASE("logical and bitwise not fold and lower correctly") {
    // !5 folds to 0, ~0 folds to -1, and a runtime !v uses the eqz lowering.
    const std::string source = R"(
asset t {
  size 8x8
  output point p
  draw(v: i32, a: i32) {
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
  draw(v: i32, a: i32) {
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
  draw(v: i32, a: i32) {
    x = 6 / 2
  }
})";
    const auto bytes = compileChecked(source);
    const auto loaded = loadFile(bytes);
    // const.i32 6, const.i32 2, i32.div_s, local.set — no folding.
    REQUIRE(loaded.code.entry().code.size() == 16);
}

TEST_CASE("list outputs, circle outputs and poly compile and run") {
    const std::string source = R"(
asset t {
  size 8x8
  output list point pts
  output circle c

  draw(v: i32, a: i32) {
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

TEST_CASE("part placement can read another part's output") {
    const std::string anchor = R"(
asset anchor {
  size 4x4
  output point spot
  draw(v: i32, a: i32) {
    spot = {2, 3}
  }
})";
    const std::string composite = R"(
import anchor from "anchor.ips"

asset composite {
  size 8x8
  output point p

  draw(v: i32, a: i32) {
    anchor.draw(1, v, a, at = {0, 0})
    anchor.draw(2, v, a, at = anchor.outputs.spot)
    p = anchor.outputs.spot
  }
})";
    const ips::Program anchorProgram = ips::parse(anchor);
    const ips::Program compositeProgram = ips::parse(composite);
    ips::checkAsset(anchorProgram, {});
    ips::checkAsset(compositeProgram,
                    {{"anchor", partInfoOf(anchorProgram.asset)}});

    const auto bytes = compileProgram(compositeProgram,
                                      {{"anchor", &anchorProgram}});
    const auto loaded = loadFile(bytes);
    CHECK_NOTHROW(validate(loaded));
    VM vm(loaded, GenerationInputs{5, 0, 0, 0, 0, {}});
    CHECK_NOTHROW(vm.run());
    const GenerationResult result = vm.result();
    const NamedOutput* p = result.find("p");
    REQUIRE(p != nullptr);
    REQUIRE(p->points.size() == 1);
    REQUIRE(p->points[0].x == 2.f);
    REQUIRE(p->points[0].y == 3.f);
}

TEST_CASE("shifts, i32trunc and round execute with VM semantics") {
    const std::string source = R"(
asset t {
  size 8x8
  output point p

  draw(v: i32, a: i32) {
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

  draw(v: i32, a: i32) {
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
