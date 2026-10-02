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
            "draw(v: i32, a: i32) { " + body + "pixel(x, flag, c) "
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

TEST_CASE("vec2 locals, operators, swizzles and helpers compile and run") {
    const std::string source = R"(
asset t {
  size 64x64
  output point p

  helper(a: vec2, s: f32) -> vec2 {
    return a * s + {1.0, 2.0}
  }

  draw(v: i32, a: i32) {
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
  draw(v: i32, a: i32) {
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
          draw(v: i32, a: i32) {
            if (v) { q = {5.0, 6.0} }
            top = q
          }
        })");
        const auto root = ips::parse(R"(import child from "child.ips"
          asset t { size 8x8 variants 2 output point p
            draw(v: i32, a: i32) {
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
  draw(v: i32, a: i32) {
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
                             "draw(v: i32,a: i32) { q={1,2} ";
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
               "draw(v: i32, a: i32) { " + body +
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
      draw(v: i32, a: i32) { top = {3, 4} x = {5, 6} y = {7, 8} }
    })");
    const auto root = ips::parse(R"(import child from "child.ips"
      asset t { size 8x8
        output point components
        output point named
        output point translated
        output point bare
        draw(v: i32, a: i32) {
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

TEST_CASE("the SDF builtins render a soft-edged circle end to end") {
    const std::string source = R"(
asset t {
  size 16x16
  output point p

  cover(q: vec2) -> f32 {
    return fill(dist.circle(q, {8.0, 8.0}, 3.0), 1.0)
  }

  draw(v: i32, a: i32) {
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

  draw(v: i32, a: i32) {
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

    // The hand-assembled .ipa reference for the f32 ramp (the same
    // desugar the compiler emits).
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

TEST_CASE("the colour builtins compile and match the hand-assembled reference") {
    const std::string source = R"(
asset t {
  size 8x8
  draw(v: i32, a: i32) {
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

TEST_CASE("vec2 source builtins use counterclockwise asset angles") {
    const auto result = run(compileChecked(R"(
asset t {
  size 8x8
  output point metrics
  output point positive
  output point negative
  output point angle
  output point integerAngle
  draw(v:i32,a:i32) {
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

TEST_CASE("round widens integer arguments exactly once") {
    const auto result = run(compileChecked(R"(
asset t {
  size 8x8
  output point result
  draw(v:i32,a:i32) { result = {round(3), round(count())} }
  count() -> i32 { pixel(0,0,#ff000080) return -3 }
})"),0,0);
    CHECK(result.outputs[0].points[0].x == 3.f);
    CHECK(result.outputs[0].points[0].y == -3.f);
    CHECK(result.canvas.at(0,0).r == 128); // a second evaluation would composite again
}

TEST_CASE("the dither, fold and polar builtins compile and match the .ipa") {
    const std::string source = R"(
asset t {
  size 8x8
  output point p

  draw(v: i32, a: i32) {
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

TEST_CASE("part transforms mirror the pixels and propagate the outputs") {
    const std::string wing = R"(
asset wing {
  size 4x4
  output point tip
  draw(v: i32, a: i32) {
    pixel(0, 0, #ff0000)
    pixel(1, 1, #00ff00)
    tip = {1, 0}
  }
})";
    const std::string composite = R"(
import wing from "wing.ips"
asset t {
  size 8x8
  output point p
  draw(v: i32, a: i32) {
    wing.draw(1, v, a, at = {0, 0})
    wing.draw(2, v, a, at = {4, 0}, mirror_x = true)
    p = wing.outputs.tip
  }
})";
    const ips::Program wingProgram = ips::parse(wing);
    const ips::Program compositeProgram = ips::parse(composite);
    ips::checkAsset(wingProgram, {});
    ips::checkAsset(compositeProgram, {{"wing", partInfoOf(wingProgram.asset)}});
    const auto bytes = compileProgram(compositeProgram, {{"wing", &wingProgram}});
    const auto loaded = loadFile(bytes);
    CHECK_NOTHROW(validate(loaded));
    VM vm(loaded, GenerationInputs{5, 0, 0, 0, 0, {}});
    CHECK_NOTHROW(vm.run());
    const GenerationResult result = vm.result();
    // The identity blit paints (0,0) and (1,1).
    REQUIRE(result.image.at(0, 0) == Color{255, 0, 0, 255});
    REQUIRE(result.image.at(1, 1) == Color{0, 255, 0, 255});
    // The mirrored blit at (4,0): (0,0) -> (7,0), (1,1) -> (6,1).
    REQUIRE(result.image.at(7, 0) == Color{255, 0, 0, 255});
    REQUIRE(result.image.at(6, 1) == Color{0, 255, 0, 255});
    REQUIRE(result.image.at(0, 0) == Color{255, 0, 0, 255});
    // The output propagates through the mirror: {1, 0} -> {3, 0}.
    const NamedOutput* p = result.find("p");
    REQUIRE(p != nullptr);
    REQUIRE(p->points.size() == 1);
    REQUIRE(p->points[0].x == 3.f);
    REQUIRE(p->points[0].y == 0.f);
}

TEST_CASE("the rot and mirror compositions fold to the D4 kinds") {
    const std::string wing = R"(
asset wing {
  size 4x4
  draw(v: i32, a: i32) {
    pixel(0, 0, #ff0000)
  }
})";
    // rot 180 with both mirrors is the identity: the pixel stays at (0,0).
    const std::string composite = R"(
import wing from "wing.ips"
asset t {
  size 4x4
  draw(v: i32, a: i32) {
    wing.draw(1, v, a, at = {0, 0}, rot = 180, mirror_x = true, mirror_y = true)
  }
})";
    const ips::Program wingProgram = ips::parse(wing);
    const ips::Program compositeProgram = ips::parse(composite);
    ips::checkAsset(wingProgram, {});
    ips::checkAsset(compositeProgram, {{"wing", partInfoOf(wingProgram.asset)}});
    const auto bytes = compileProgram(compositeProgram, {{"wing", &wingProgram}});
    const auto loaded = loadFile(bytes);
    CHECK_NOTHROW(validate(loaded));
    VM vm(loaded, GenerationInputs{5, 0, 0, 0, 0, {}});
    CHECK_NOTHROW(vm.run());
    REQUIRE(vm.result().image.at(0, 0) == Color{255, 0, 0, 255});

    // The rot 90 of the 4x4 part lands (0,0) at (0,3) — the (dx, dy) =
    // (sy, w-1-sx) mapping (spec §10.5, y grows downward).
    const std::string rotated = R"(
import wing from "wing.ips"
asset t {
  size 4x4
  draw(v: i32, a: i32) {
    wing.draw(1, v, a, at = {0, 0}, rot = 90)
  }
})";
    const ips::Program rotatedProgram = ips::parse(rotated);
    ips::checkAsset(rotatedProgram, {{"wing", partInfoOf(wingProgram.asset)}});
    const auto rotatedBytes =
        compileProgram(rotatedProgram, {{"wing", &wingProgram}});
    const auto rotatedLoaded = loadFile(rotatedBytes);
    CHECK_NOTHROW(validate(rotatedLoaded));
    VM rotatedVm(rotatedLoaded, GenerationInputs{5, 0, 0, 0, 0, {}});
    CHECK_NOTHROW(rotatedVm.run());
    REQUIRE(rotatedVm.result().image.at(0, 3) == Color{255, 0, 0, 255});
}

TEST_CASE("part transform values are checked at the compile") {
    const std::string wing = R"(
asset wing {
  size 4x4
  draw(v: i32, a: i32) {
    pixel(0, 0, #ff0000)
  }
})";
    const ips::Program wingProgram = ips::parse(wing);
    const auto compileComposite = [&](const std::string& source,
                                      const char* messagePart) {
        bool threw = false;
        try {
            const ips::Program program = ips::parse(source);
            ips::checkAsset(program, {{"wing", partInfoOf(wingProgram.asset)}});
            (void)compileProgram(program, {{"wing", &wingProgram}});
        } catch (const ips::SourceError& error) {
            threw = true;
            REQUIRE(std::string(error.message).find(messagePart) !=
                    std::string::npos);
        }
        REQUIRE(threw);
    };
    compileComposite(
        "import wing from \"wing.ips\"\nasset t { size 4x4 "
        "draw(v: i32, a: i32) { wing.draw(1, v, a, at = {0, 0}, rot = 45) } }",
        "the rot transform must be 0, 90, 180 or 270");
    compileComposite(
        "import wing from \"wing.ips\"\nasset t { size 4x4 "
        "draw(v: i32, a: i32) { wing.draw(1, v, a, at = {0, 0}, mirror_x = 2) } }",
        "the mirror_x transform must be 0 or 1");
    compileComposite(
        "import wing from \"wing.ips\"\nasset t { size 4x4 "
        "draw(v: i32, a: i32) { wing.draw(1, v, a, at = {0, 0}, mirror_x = 1.5) } }",
        "must be an integer");
    for (const std::string name : {"rot", "mirror_x", "mirror_y"}) {
        compileComposite(
            "import wing from \"wing.ips\"\nasset t { size 4x4 "
            "draw(v: i32, a: i32) { wing.draw(1, v, a, at = {0, 0}, " +
                name + " = 0, " + name + " = 0) } }",
            "duplicate");
    }
    compileComposite(
        "import wing from \"wing.ips\"\nasset t { size 4x4 "
        "draw(v: i32, a: i32) { wing.draw(1, v, a, at = {0, 0}, rot = v) } }",
        "constant integers");
}

TEST_CASE("part helper writes inherit transforms and assembly preserves every D4 kind") {
    const ips::Program part = ips::parse(R"(
asset tile {
  size 4x2
  output point direct
  output point anchor
  output rect box
  output circle zone
  output list point anchors
  output list rect boxes
  output list circle zones
  draw(v: i32, a: i32) {
    pixel(0, 0, #ff0000)
    pixel(3, 1, #0000ff)
    direct = {1, 0.5}
    writeOutputs()
  }
  writeOutputs() {
    anchor = {1, 0.5}
    box = {0.5, 0.25, 2, 1}
    zone = {1, 0.5, 0.25}
    anchors = {1, 0.5}
    boxes = {0.5, 0.25, 2, 1}
    zones = {1, 0.5, 0.25}
  }
})");
    ips::checkAsset(part, {});
    // Independent expected coordinates for the two coloured source pixels,
    // the anchor, and the bounding rectangle of this non-square part.
    const i32 red[8][2] = {{0,0}, {0,3}, {3,1}, {1,0}, {3,0}, {0,1}, {0,0}, {1,3}};
    const i32 blue[8][2] = {{3,1}, {1,0}, {0,0}, {0,3}, {0,1}, {3,0}, {1,3}, {0,0}};
    const f32 points[8][2] = {{1,.5}, {.5,3}, {3,1.5}, {1.5,1}, {3,.5}, {1,1.5}, {.5,1}, {1.5,3}};
    const f32 rects[8][4] = {{.5,.25,2,1}, {.25,1.5,1,2}, {1.5,.75,2,1}, {.75,.5,1,2},
                            {1.5,.25,2,1}, {.5,.75,2,1}, {.25,.5,1,2}, {.75,1.5,1,2}};
    // All 16 combinations, with a literal composition table used only to
    // select the independent expected data above.
    const u8 kinds[4][2][2] = {{{0,5},{4,2}}, {{1,6},{7,3}}, {{2,4},{5,0}}, {{3,7},{6,1}}};
    for (i32 rot = 0; rot < 4; ++rot) {
        for (i32 mx = 0; mx < 2; ++mx) {
            for (i32 my = 0; my < 2; ++my) {
                const u8 kind = kinds[rot][mx][my];
                INFO("rot=" << rot * 90 << " mirror_x=" << mx << " mirror_y=" << my);
                const auto root = ips::parse(
                    "import tile from \"tile.ips\"\nasset t { size 8x8 output point p "
                    "draw(v: i32, a: i32) { tile.draw(1, v, a, at = {0,0}, rot = " +
                    std::to_string(rot * 90) + ", mirror_x = " + std::to_string(mx) +
                    ", mirror_y = " + std::to_string(my) + ") p = tile.outputs.anchor } }");
                ips::checkAsset(root, {{"tile", partInfoOf(part.asset)}});
                const auto bytes = compileProgram(root, {{"tile", &part}});
                const auto original = execute(bytes);
                const auto restored = execute(assemble(disassemble(bytes)));
                CHECK(restored.canvas.pixels == original.canvas.pixels);
                for (const auto* result : {&original, &restored}) {
                    CHECK(result->canvas.at(red[kind][0], red[kind][1]) == Color{255,0,0,255});
                    CHECK(result->canvas.at(blue[kind][0], blue[kind][1]) == Color{0,0,255,255});
                    for (const std::size_t slot : {std::size_t{0}, std::size_t{1}, std::size_t{2}, std::size_t{5}}) {
                        REQUIRE(result->outputs[slot].points.size() == 1);
                        CHECK(result->outputs[slot].points[0].x == points[kind][0]);
                        CHECK(result->outputs[slot].points[0].y == points[kind][1]);
                    }
                    for (const std::size_t slot : {std::size_t{3}, std::size_t{6}}) {
                        REQUIRE(result->outputs[slot].rects.size() == 1);
                        const auto& box = result->outputs[slot].rects[0];
                        CHECK(box.x == rects[kind][0]);
                        CHECK(box.y == rects[kind][1]);
                        CHECK(box.w == rects[kind][2]);
                        CHECK(box.h == rects[kind][3]);
                    }
                    for (const std::size_t slot : {std::size_t{4}, std::size_t{7}}) {
                        REQUIRE(result->outputs[slot].circles.size() == 1);
                        const auto& zone = result->outputs[slot].circles[0];
                        CHECK(zone.x == points[kind][0]);
                        CHECK(zone.y == points[kind][1]);
                        CHECK(zone.r == .25f);
                    }
                }
            }
        }
    }
}

TEST_CASE("transformed rect bounds use pinned NaN and signed-zero minNum") {
    const auto part = ips::parse(R"(
asset tile {
  size 4x2
  output rect zeros
  output rect nan_box
  draw(v: i32, a: i32) {
    zeros = {-0.0, -0.0, 0.0, 0.0}
    nan = -pow(-2, 0.5)
    nan_box = {nan, nan, 0, 0}
  }
})");
    const auto root = ips::parse(R"(
import tile from "tile.ips"
asset t {
  size 8x8
  draw(v: i32, a: i32) {
    tile.draw(1, v, a, at = {0,0}, rot = 90, mirror_y = true)
  }
})");
    ips::checkAsset(part, {});
    ips::checkAsset(root, {{"tile", partInfoOf(part.asset)}});
    const auto result = execute(compileProgram(root, {{"tile", &part}}));
    const auto& zeros = result.outputs[0].rects[0];
    CHECK(std::bit_cast<u32>(zeros.x) == 0x80000000u);
    CHECK(std::bit_cast<u32>(zeros.y) == 0x80000000u);
    CHECK(std::bit_cast<u32>(zeros.w) == 0u);
    CHECK(std::bit_cast<u32>(zeros.h) == 0u);
    const auto& nan = result.outputs[1].rects[0];
    CHECK(std::bit_cast<u32>(nan.x) == 0x7FC00000u);
    CHECK(std::bit_cast<u32>(nan.y) == 0x7FC00000u);
    CHECK(std::isnan(nan.w));
    CHECK(std::isnan(nan.h));
}

TEST_CASE("two compilations are byte-identical") {
    REQUIRE(compileChecked(kRockSource) == compileChecked(kRockSource));
    REQUIRE(compileChecked(kFixture) == compileChecked(kFixture));
}

TEST_CASE("quadratic and cubic Bezier queries and strokes survive assembly round trips") {
    const std::string source = R"(
asset t {
  size 8x8
  output point quadratic
  output point cubic
  output point bits
  draw(v: i32, a: i32) {
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
        const auto program = ips::parse("asset t { size 8x8 draw(v: i32, a: i32) { " + call + " } }");
        INFO(call);
        REQUIRE_THROWS_AS(ips::checkAsset(program, {}), ips::SourceError);
        REQUIRE_THROWS_AS(compileProgram(program, {}), ips::SourceError);
    }
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

TEST_CASE("two aliases of the same imported program keep independent output slots") {
    const auto part = ips::parse(R"(asset child { size 8x8 variants 2
        output point p
        draw(v: i32, a: i32) { p = {v + 1, 0} } })");
    const auto root = ips::parse(R"(import first from "child.ips"
        import second from "child.ips"
        asset root { size 8x8
          draw(v: i32, a: i32) {
            first.draw(seed, 0, a, at = {0, 0})
            second.draw(seed, 1, a, at = {0, 0})
          } })");
    ips::checkAsset(part, {});
    ips::checkAsset(root, {{"first", partInfoOf(part.asset)},
                           {"second", partInfoOf(part.asset)}});
    const auto result = execute(compileProgram(root,
        {{"first", &part}, {"second", &part}}));
    REQUIRE(result.outputs.size() == 2);
    REQUIRE(result.outputs[0].points[0].x == 1.f);
    REQUIRE(result.outputs[1].points[0].x == 2.f);
}

TEST_CASE("helpers resolve within the root or owning part namespace") {
    const auto first = ips::parse(R"(asset first { size 8x8 output point p
      draw(v: i32, a: i32) { p = {helper(), 0} }
      helper() -> i32 { return 1 } })");
    const auto second = ips::parse(R"(asset second { size 8x8 output point p
      draw(v: i32, a: i32) { p = {helper(2.5, 1), 0} }
      helper(x: f32, y: i32) -> f32 { return x + y } })");
    const auto root = ips::parse(R"(import first from "first.ips"
      import second from "second.ips"
      asset root { size 8x8 output point p
        draw(v: i32, a: i32) {
          first.draw(seed, v, a, at = {0, 0})
          second.draw(seed, v, a, at = {0, 0})
          p = {helper(7), 0}
        }
        helper(x: i32) -> i32 { return x }
      })");
    ips::checkAsset(first, {});
    ips::checkAsset(second, {});
    ips::checkAsset(root, {{"first", partInfoOf(first.asset)},
                           {"second", partInfoOf(second.asset)}});
    const auto result = execute(compileProgram(root,
        {{"first", &first}, {"second", &second}}));
    REQUIRE(result.outputs.size() == 3);
    REQUIRE(result.outputs[0].points[0].x == 7.f);
    REQUIRE(result.outputs[1].points[0].x == 1.f);
    REQUIRE(result.outputs[2].points[0].x == 3.5f);
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
