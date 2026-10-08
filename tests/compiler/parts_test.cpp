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
  draw {
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
  draw {
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

  draw {
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

    // The child output surfaces in the parent's coordinates: the pupil sits
    // on the part's origin, which the call placed at {32, 24}.
    const NamedOutput* pupil = result.find("pupilCenter");
    REQUIRE(pupil != nullptr);
    REQUIRE(pupil->points.size() == 1);
    REQUIRE(pupil->points[0].x == 32.f);
    REQUIRE(pupil->points[0].y == 24.f);

    // The parts' own outputs are present in global slot order, in root
    // coordinates for the host.
    REQUIRE(result.find("top") != nullptr);
    CHECK(result.find("top")->points[0].x == 32.f);
    CHECK(result.find("top")->points[0].y == 4.f);
    REQUIRE(result.find("pupil") != nullptr);
    CHECK(result.find("pupil")->points[0].y == 24.f);

    // Deterministic.
    REQUIRE(compileProgram(portraitProgram, parts) == bytes);
}

TEST_CASE("two aliases of the same imported program keep independent output slots") {
    const auto part = ips::parse(R"(asset child { size 8x8 variants 2
        output point p
        draw { p = {v + 1, 0} } })");
    const auto root = ips::parse(R"(import first from "child.ips"
        import second from "child.ips"
        asset root { size 8x8
          draw {
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
      draw { p = {helper(), 0} }
      helper() -> i32 { return 1 } })");
    const auto second = ips::parse(R"(asset second { size 8x8 output point p
      draw { p = {helper(2.5, 1), 0} }
      helper(x: f32, y: i32) -> f32 { return x + y } })");
    const auto root = ips::parse(R"(import first from "first.ips"
      import second from "second.ips"
      asset root { size 8x8 output point p
        draw {
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
      draw { p = {width, 0} } })");
    const auto root = ips::parse(R"(import child from "child.ips"
      asset root { size 8x8
        draw { child.draw(seed, v, a, at = {0,0}, width = measure()) }
        measure() -> i32 { pixel(0,0,#ffffff80) return 3 }
      })");
    ips::checkAsset(part, {});
    ips::checkAsset(root, {{"child",partInfoOf(part.asset)}});
    const auto result = execute(compileProgram(root, {{"child",&part}}));
    REQUIRE(result.outputs.size() == 1);
    CHECK(result.outputs[0].points[0].x == 3.0f);
    CHECK(result.canvas.pixels[0].r == 128);
    const auto duplicate = ips::parse(R"(import child from "child.ips"
      asset root { size 8x8 draw {
        child.draw(seed,v,a,at={0,0},width=1,width=2)
      } })");
    CHECK_THROWS_AS(ips::checkAsset(duplicate, {{"child",partInfoOf(part.asset)}}), ips::SourceError);
    CHECK_THROWS_AS(compileProgram(duplicate, {{"child",&part}}), ips::SourceError);
}

TEST_CASE("part placement can read another part's output") {
    const std::string anchor = R"(
asset anchor {
  size 4x4
  output point spot
  draw {
    spot = {2, 3}
  }
})";
    const std::string composite = R"(
import anchor from "anchor.ips"

asset composite {
  size 8x8
  output point p

  draw {
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
    // Reads are in the composite's coordinates: the first call puts the spot
    // at {2, 3}; the second call, drawn there, moves it to {4, 6}.
    const NamedOutput* p = result.find("p");
    REQUIRE(p != nullptr);
    REQUIRE(p->points.size() == 1);
    REQUIRE(p->points[0].x == 4.f);
    REQUIRE(p->points[0].y == 6.f);
}

namespace {
void comparePartCallResults(const GenerationResult& actual, const GenerationResult& expected) {
    CHECK(actual.image.pixels == expected.image.pixels);
    REQUIRE(actual.outputs.size() == expected.outputs.size());
    const auto bits = [](f32 value) { return std::bit_cast<u32>(value); };
    for (std::size_t i = 0; i < actual.outputs.size(); ++i) {
        const auto& a = actual.outputs[i];
        const auto& b = expected.outputs[i];
        CHECK(a.name == b.name);
        CHECK(a.kind == b.kind);
        CHECK(a.isList == b.isList);
        REQUIRE(a.points.size() == b.points.size());
        REQUIRE(a.rects.size() == b.rects.size());
        REQUIRE(a.circles.size() == b.circles.size());
        for (std::size_t j = 0; j < a.points.size(); ++j) {
            CHECK(bits(a.points[j].x) == bits(b.points[j].x));
            CHECK(bits(a.points[j].y) == bits(b.points[j].y));
        }
        for (std::size_t j = 0; j < a.rects.size(); ++j) {
            CHECK(bits(a.rects[j].x) == bits(b.rects[j].x));
            CHECK(bits(a.rects[j].y) == bits(b.rects[j].y));
            CHECK(bits(a.rects[j].w) == bits(b.rects[j].w));
            CHECK(bits(a.rects[j].h) == bits(b.rects[j].h));
        }
        for (std::size_t j = 0; j < a.circles.size(); ++j) {
            CHECK(bits(a.circles[j].x) == bits(b.circles[j].x));
            CHECK(bits(a.circles[j].y) == bits(b.circles[j].y));
            CHECK(bits(a.circles[j].r) == bits(b.circles[j].r));
        }
    }
}

GenerationResult generatePartCall(const std::vector<u8>& bytes, u32 seed, u32 v = 0, u32 a = 0) {
    const auto loaded = loadFile(bytes);
    validate(loaded);
    VM vm(loaded, GenerationInputs{seed, v, a, 0, 0, {}});
    vm.run();
    return vm.result();
}
}

TEST_CASE("named and mixed part controls preserve legacy bytes outputs and defaults", "[part-named-controls]") {
    const auto part = ips::parse(R"(asset child { size 4x4 variants 3 archetypes 4
      input i32 variant = 7
      input i32 archetype = 9
      input f32 amount = -0.0
      input color tint = #f00
      output point controls
      output point uniforms
      output point random
      output rect box
      output circle zone
      draw {
        controls = {v, a}
        uniforms = {variant, archetype}
        random = {atan2(amount, -1.0), unit()}
        box = {0, 0, 2, 3}
        zone = {1, 2, 1}
        rect(0, 0, 3, 2, tint)
      }
    })");
    ips::checkAsset(part, {});
    const auto root = [](const std::string& arguments) {
        return ips::parse("import child from \"child.ips\"\n"
            "asset root { size 16x16 variants 3 archetypes 4 output point after\n"
            "draw { child.draw(" + arguments + ") after = {unit(), unit()} } }");
    };
    const std::vector<std::vector<std::string>> forms{
        {
            "seed ^ 17, v, a, at={8,4},variant=12,archetype=-3,amount=2,tint=#abc,rot=90,mirror_x=1",
            "a=a,tint=#abc,v=v,archetype=-3,seed=seed ^ 17,at={8,4},variant=12,amount=2,mirror_x=1,rot=90",
            "a=a,seed ^ 17,variant=12,v,at={8,4},amount=2,archetype=-3,tint=#abc,rot=90,mirror_x=1",
            "seed ^ 17,v=v,a=a,at={8,4},variant=12,archetype=-3,amount=2,tint=#abc,rot=90,mirror_x=1"
        },
        {"seed,v,a,at={8,4}", "at={8,4},a=a,v=v,seed=seed", "a=a,seed,v,at={8,4}"}
    };
    for (const auto& group : forms) {
        const auto positional = root(group.front());
        const auto reference = compileProgram(positional, {{"child", &part}}, false);
        for (const auto& arguments : group) {
            INFO(arguments);
            const auto program = root(arguments);
            CHECK_NOTHROW(ips::checkAsset(program, {{"child", partInfoOf(part.asset)}}));
            for (const bool optimize : {false, true}) {
                const auto bytes = compileProgram(program, {{"child", &part}}, optimize);
                CHECK(bytes == compileProgram(positional, {{"child", &part}}, optimize));
                for (const u32 seed : {0u, 7u, 0x80000000u, 0xffffffffu}) {
                    for (const u32 v : {0u, 2u}) {
                        for (const u32 a : {0u, 3u}) {
                            comparePartCallResults(generatePartCall(bytes, seed, v, a),
                                                   generatePartCall(reference, seed, v, a));
                        }
                    }
                }
            }
        }
    }
    // Published in root coordinates: the child is placed at {8, 4}.
    const auto defaults = generatePartCall(compileProgram(root(forms[1][1]), {{"child", &part}}), 42, 2, 3);
    REQUIRE(defaults.find("controls") != nullptr);
    CHECK(defaults.find("controls")->points[0].x == 10.f);
    CHECK(defaults.find("controls")->points[0].y == 7.f);
    REQUIRE(defaults.find("uniforms") != nullptr);
    CHECK(defaults.find("uniforms")->points[0].x == 15.f);
    CHECK(defaults.find("uniforms")->points[0].y == 13.f);
    // The -0.0 default reaches the part: atan2(-0, -1) is -pi.
    CHECK(defaults.find("random")->points[0].x < 8.f);
}

TEST_CASE("named part controls evaluate uniforms controls and placement once before isolated RNG", "[part-named-controls]") {
    const auto part = ips::parse(R"(asset child { size 2x2
      input f32 variant = 1.0
      input i32 archetype = 2
      output list point childTrace
      draw { for i in 0..12 { childTrace = {i, unit()} } }
    })");
    const auto root = [](const std::string& body) {
        return ips::parse("import child from \"child.ips\"\n"
            "asset root { size 16x16 output list point trace draw { " + body + " note(8) }\n"
            "note(tag:i32) -> i32 { trace={tag,unit()} pixel(tag,0,#ffffff80) return 0 } }");
    };
    const auto reference = root(
        "child.draw(note(3),note(4),note(5),at={note(6),note(7)},archetype=note(2),variant=note(1))");
    const auto baseline = ips::parse(R"(asset root { size 16x16 output list point trace
      draw { for i in 1..9 { trace={i,unit()} pixel(i,0,#ffffff80) } }
    })");
    for (const auto* arguments : {
        "at={note(6),note(7)},a=note(5),archetype=note(2),seed=note(3),v=note(4),variant=note(1)",
        "a=note(5),note(3),archetype=note(2),note(4),variant=note(1),at={note(6),note(7)}"
    }) {
        const auto program = root("child.draw(" + std::string(arguments) + ")");
        for (const bool optimize : {false, true}) {
            const auto bytes = compileProgram(program, {{"child", &part}}, optimize);
            CHECK(bytes == compileProgram(reference, {{"child", &part}}, optimize));
            for (const u32 seed : {0u, 7u, 0xffffffffu}) {
                const auto actual = generatePartCall(bytes, seed);
                comparePartCallResults(actual, generatePartCall(
                    compileProgram(reference, {{"child", &part}}, false), seed));
                const auto outer = generatePartCall(compileProgram(baseline, {}, false), seed);
                CHECK(actual.image.pixels == outer.image.pixels);
                REQUIRE(actual.find("trace") != nullptr);
                REQUIRE(actual.find("childTrace") != nullptr);
                const auto& trace = actual.find("trace")->points;
                REQUIRE(trace.size() == 8);
                CHECK(actual.find("childTrace")->points.size() == 12);
                for (std::size_t i = 0; i < trace.size(); ++i) {
                    CHECK(trace[i].x == static_cast<f32>(i + 1));
                    CHECK(std::bit_cast<u32>(trace[i].y) == std::bit_cast<u32>(outer.outputs[0].points[i].y));
                    CHECK(actual.image.at(static_cast<u32>(i + 1), 0).r == 128);
                }
            }
        }
    }
}

TEST_CASE("named part control binding rejects missing duplicate unknown excessive and mistyped arguments", "[part-named-controls]") {
    const auto part = ips::parse(R"(asset child { size 2x2
      input i32 variant = 1
      input i32 archetype = 2
      draw { }
    })");
    struct Case { const char* arguments; const char* message; };
    for (const auto test : {
        Case{"v=0,a=0,at={0,0}", "missing argument 'seed'"},
        Case{"seed=0,a=0,at={0,0}", "missing argument 'v'"},
        Case{"seed=0,v=0,at={0,0}", "missing argument 'a'"},
        Case{"seed=0,v=0,a=0", "missing argument 'at'"},
        Case{"0,0,0,1,at={0,0}", "too many positional arguments"},
        Case{"0,0,0,{0,0}", "too many positional arguments"},
        Case{"seed=0,0,v=0,a=0,at={0,0}", "duplicate 'seed'"},
        Case{"seed=0,v=0,v=1,a=0,at={0,0}", "duplicate 'v'"},
        Case{"seed=0,v=0,a=0,a=1,at={0,0}", "duplicate 'a'"},
        Case{"seed=0,v=0,a=0,at={0,0},at={1,1}", "duplicate 'at'"},
        Case{"seed=0,v=0,a=0,at={0,0},varaint=2", "has no argument 'varaint'"},
        Case{"seed=0.5,v=0,a=0,at={0,0}", "must be an integer"},
        Case{"seed=0,v=#fff,a=0,at={0,0}", "must be an integer"},
        Case{"seed=0,v=0,a={0,0},at={0,0}", "must be an integer"},
        Case{"seed=0,v=0,a=0,at=0", "'at' must be a vec2"},
        Case{"seed=0,v=0,a=0,at={0,0},variant=1.5", "must be i32, got f32"},
        Case{"seed=0,v=0,a=0,at={0,0},archetype=#fff", "must be i32, got color"},
        Case{"seed=0,v=0,a=0,at={0,0},variant=1,variant=2", "duplicate 'variant'"}
    }) {
        INFO(test.arguments);
        const auto program = ips::parse("import child from \"child.ips\"\nasset root { size 8x8 draw { child.draw(" +
                                        std::string(test.arguments) + ") } }");
        for (const bool semanticOnly : {true, false}) {
            try {
                if (semanticOnly) ips::checkAsset(program, {{"child", partInfoOf(part.asset)}});
                else (void)compileProgram(program, {{"child", &part}});
                FAIL("invalid part argument was accepted");
            } catch (const ips::SourceError& error) {
                CHECK(error.message.find(test.message) != std::string::npos);
                CHECK(error.stage == ips::DiagnosticStage::Typing);
            }
        }
    }
}
