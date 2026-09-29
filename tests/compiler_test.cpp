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
