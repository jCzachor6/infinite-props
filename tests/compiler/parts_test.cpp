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

TEST_CASE("part uniform arguments widen once and reject duplicate overrides") {
    const auto part = ips::parse(R"(asset child { size 8x8
      input f32 width = 1.0
      output point p
      draw(v:i32,a:i32) { p = {width, 0} } })");
    const auto root = ips::parse(R"(import child from "child.ips"
      asset root { size 8x8
        draw(v:i32,a:i32) { child.draw(seed, v, a, at = {0,0}, width = measure()) }
        measure() -> i32 { pixel(0,0,#ffffff80) return 3 }
      })");
    ips::checkAsset(part, {});
    ips::checkAsset(root, {{"child",partInfoOf(part.asset)}});
    const auto result = execute(compileProgram(root, {{"child",&part}}));
    REQUIRE(result.outputs.size() == 1);
    CHECK(result.outputs[0].points[0].x == 3.0f);
    CHECK(result.canvas.pixels[0].r == 128);
    const auto duplicate = ips::parse(R"(import child from "child.ips"
      asset root { size 8x8 draw(v:i32,a:i32) {
        child.draw(seed,v,a,at={0,0},width=1,width=2)
      } })");
    CHECK_THROWS_AS(ips::checkAsset(duplicate, {{"child",partInfoOf(part.asset)}}), ips::SourceError);
    CHECK_THROWS_AS(compileProgram(duplicate, {{"child",&part}}), ips::SourceError);
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

