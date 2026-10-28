#include <catch2/catch_test_macros.hpp>

#include "SourceCompilation.hpp"
#include "iprop/VM.hpp"
#include "ips/Parser.hpp"
#include "ips/Printer.hpp"
#include "support/compiler_support.hpp"

#include <map>
#include <string>

using namespace iprop;

namespace {
const std::map<std::string, std::string> kSources{
    {"entry.ips", R"(import bark from "bark.ips";
        asset tree { size 16x16
          draw {
            m = bark.Bark()
            branch(8.0, 15.0, 5.0, m)
            pixel(1, 1, m.shadow)
          }
          branch(x: f32, y: f32, w: f32, m: bark.Bark) {
            line(x, y, x, y - w, int(m.edge_width) + 1, m.base)
            pixel(int(x), int(y - w) - 1, m.edge)
          }
        })"},
    {"bark.ips", R"(material Bark {
          base = #5a4230 to #7a5c40
          shadow = #2a1a10
          highlight = #9a7a54
          edge = color.mix(base, #000000, 0.5)
          taper = 0.5 to 0.8
          edge_width = 1
        })"},
};

auto loadFrom(const std::map<std::string, std::string>& sources) {
    return [sources](std::string_view path) { return sources.at(std::string(path)); };
}
} // namespace

TEST_CASE("material from a separate file compiles and renders deterministically") {
    const auto result = compileSource("entry.ips", loadFrom(kSources));
    REQUIRE(result.status == SourceCompilationStatus::Success);
    REQUIRE_FALSE(result.bytes.empty());
    const auto first = compiler_test::execute(result.bytes, 7);
    const auto again = compiler_test::execute(result.bytes, 7);
    CHECK(first.canvas.pixels == again.canvas.pixels);
    bool anyPainted = false;
    for (const auto pixel : first.canvas.pixels) anyPainted = anyPainted || pixel.a != 0;
    CHECK(anyPainted);
}

TEST_CASE("material defined in an asset file is rejected") {
    const std::map<std::string, std::string> sources{
        {"entry.ips", "material Bark { base = #112233 } asset t { size 8x8 draw {} }"}};
    const auto result = compileSource("entry.ips", loadFrom(sources));
    CHECK(result.status == SourceCompilationStatus::InvalidSource);
    CHECK_FALSE(result.diagnostics.empty());
}

TEST_CASE("material without base is rejected") {
    auto sources = kSources;
    sources["bark.ips"] = "material Bark { shadow = #2a1a10 }";
    const auto result = compileSource("entry.ips", loadFrom(sources));
    CHECK(result.status == SourceCompilationStatus::InvalidSource);
}

TEST_CASE("unknown material field is rejected") {
    auto sources = kSources;
    sources["entry.ips"] = R"(import bark from "bark.ips";
        asset tree { size 16x16 draw { m = bark.Bark(); pixel(1, 1, m.missing); } })";
    const auto result = compileSource("entry.ips", loadFrom(sources));
    CHECK(result.status == SourceCompilationStatus::InvalidSource);
}

namespace {
std::string firstMessage(const SourceCompilationResult& result) {
    return result.diagnostics.empty() ? std::string{} : result.diagnostics.front().message;
}
} // namespace

TEST_CASE("formatting a material user prints the source as written") {
    const auto result = compileSource("entry.ips", loadFrom(kSources));
    REQUIRE(result.status == SourceCompilationStatus::Success);
    const auto& entry = result.programs.at("entry.ips");
    const auto formatted = ips::formatProgram(entry, {{"bark", &result.programs.at("bark.ips")}});
    CHECK(formatted.find("m = bark.Bark()") != std::string::npos);
    CHECK(formatted.find("m: bark.Bark") != std::string::npos);
    CHECK(formatted.find("__") == std::string::npos);
    std::map<std::string, std::string> again = kSources;
    again["entry.ips"] = formatted;
    CHECK(compileSource("entry.ips", loadFrom(again)).status == SourceCompilationStatus::Success);
}

TEST_CASE("calls to helpers of a material user keep their argument errors") {
    for (const char* call : {"branch(8.0, 15.0, 5.0, m, 9)", "branch(8.0, 15.0, 5.0, m, typo = 1)",
                             "branch(8.0, 15.0, w = 5.0, w = 4.0, m = m)"}) {
        auto sources = kSources;
        sources["entry.ips"] = std::string(R"(import bark from "bark.ips";
            asset tree { size 16x16
              draw { m = bark.Bark(); )") + call + R"( }
              branch(x: f32, y: f32, w: f32, m: bark.Bark) { pixel(int(x), int(y - w), m.base) }
            })";
        INFO(call);
        CHECK(compileSource("entry.ips", loadFrom(sources)).status == SourceCompilationStatus::InvalidSource);
    }
}

TEST_CASE("material file palettes and consts are private to that file") {
    const std::map<std::string, std::string> sources{
        {"entry.ips", R"(import a from "a.ips";
            import b from "b.ips";
            asset t { size 8x8
              palette SHADES = [#000000, #ffffff]
              const f32 K = 2.0
              draw { m = a.Bark(); n = b.Leaf(); paint(m); pixel(1, 1, n.base); pixel(2, 2, SHADES[0]) }
              paint(m: a.Bark) { pixel(0, 0, m.base); pixel(int(m.bend * K), 0, m.base) }
            })"},
        {"a.ips", R"(const f32 K = 0.5
            palette SHADES = [#5a4230, #7a5c40]
            material Bark { base = SHADES[1]; bend = K })"},
        {"b.ips", R"(const f32 K = 0.25
            material Leaf { base = #2a6a44; bend = K })"},
    };
    const auto result = compileSource("entry.ips", loadFrom(sources));
    INFO(firstMessage(result));
    CHECK(result.status == SourceCompilationStatus::Success);
}

TEST_CASE("errors in a material file are reported against that file") {
    auto sources = kSources;
    sources["bark.ips"] = "const f32 K = #ffffff\nmaterial Bark { base = #5a4230; taper = K }";
    auto result = compileSource("entry.ips", loadFrom(sources));
    REQUIRE(result.status == SourceCompilationStatus::InvalidSource);
    REQUIRE_FALSE(result.diagnostics.empty());
    CHECK(result.diagnostics.front().path == "bark.ips");

    sources["bark.ips"] = "material Bark { base = 1.0 }";
    result = compileSource("entry.ips", loadFrom(sources));
    REQUIRE_FALSE(result.diagnostics.empty());
    CHECK(result.diagnostics.front().path == "bark.ips");
    CHECK(result.diagnostics.front().location->line == 1);
    CHECK(firstMessage(result).find("'base' must be a color") != std::string::npos);
}

TEST_CASE("materials are qualified by their import alias") {
    std::map<std::string, std::string> sources{
        {"entry.ips", "import a from \"a.ips\";\nimport b from \"b.ips\";\n"
                      "asset t { size 8x8 draw { m = a.Bark(); n = b.Bark(); pixel(0, 0, m.base); "
                      "pixel(1, 0, n.base) } }"},
        {"a.ips", "material Bark { base = #000000 }"},
        {"b.ips", "material Bark { base = #ffffff }"},
    };
    auto result = compileSource("entry.ips", loadFrom(sources));
    INFO(firstMessage(result));
    CHECK(result.status == SourceCompilationStatus::Success);

    sources["entry.ips"] = "import a from \"a.ips\";\nasset t { size 8x8 draw { m = Bark(); pixel(0, 0, m.base) } }";
    result = compileSource("entry.ips", loadFrom(sources));
    REQUIRE(result.status == SourceCompilationStatus::InvalidSource);
    CHECK(firstMessage(result).find("a.Bark()") != std::string::npos);
}

TEST_CASE("a material field can read the fields before it") {
    auto sources = kSources;
    sources["bark.ips"] = "material Bark { base = #000000 to #ffffff; shadow = color.mix(base, #000000, 0.5); "
                          "edge = base; taper = 1.0; edge_width = 0; highlight = #ffffff }";
    sources["entry.ips"] = R"(import bark from "bark.ips";
        asset tree { size 4x1 draw { m = bark.Bark(); pixel(0, 0, m.base); pixel(1, 0, m.edge) } })";
    auto result = compileSource("entry.ips", loadFrom(sources));
    INFO(firstMessage(result));
    REQUIRE(result.status == SourceCompilationStatus::Success);
    const auto run = compiler_test::execute(result.bytes, 3);
    CHECK(run.canvas.pixels[0] == run.canvas.pixels[1]);

    sources["bark.ips"] = "material Bark { shadow = base; base = #000000 }";
    result = compileSource("entry.ips", loadFrom(sources));
    REQUIRE(result.status == SourceCompilationStatus::InvalidSource);
    CHECK(result.diagnostics.front().path == "bark.ips");
}

TEST_CASE("material creation can override fields by name") {
    auto sources = kSources;
    sources["entry.ips"] = R"(import bark from "bark.ips";
        asset tree { size 4x1 draw { m = bark.Bark(base = #ff0000); pixel(0, 0, m.base); pixel(1, 0, m.edge) } })";
    auto result = compileSource("entry.ips", loadFrom(sources));
    INFO(firstMessage(result));
    REQUIRE(result.status == SourceCompilationStatus::Success);
    const auto run = compiler_test::execute(result.bytes, 3);
    CHECK(run.canvas.pixels[0].r == 255);
    CHECK(run.canvas.pixels[0].g == 0);
    // `edge` mixes the overridden base, not the definition's range.
    CHECK(run.canvas.pixels[1].g == 0);

    for (const char* create : {"bark.Bark(#ff0000)", "bark.Bark(missing = 1)",
                               "bark.Bark(base = #ff0000, base = #00ff00)", "bark.Bark(taper = #ff0000)"}) {
        sources["entry.ips"] = std::string(R"(import bark from "bark.ips";
            asset tree { size 4x1 draw { m = )") + create + R"(; pixel(0, 0, m.base) } })";
        INFO(create);
        CHECK(compileSource("entry.ips", loadFrom(sources)).status == SourceCompilationStatus::InvalidSource);
    }
}

TEST_CASE("diagnostics spell expanded material fields as written") {
    auto sources = kSources;
    sources["entry.ips"] = R"(import bark from "bark.ips";
        asset tree { size 4x1 draw { m = bark.Bark(taper = 1); pixel(0, 0, m.base) } })";
    const auto result = compileSource("entry.ips", loadFrom(sources));
    REQUIRE(result.status == SourceCompilationStatus::InvalidSource);
    CHECK(firstMessage(result).find("m.taper") != std::string::npos);
    CHECK(firstMessage(result).find("__") == std::string::npos);
}

TEST_CASE("a name reassigned to a plain value is no longer a material") {
    auto sources = kSources;
    sources["entry.ips"] = R"(import bark from "bark.ips";
        asset tree { size 4x1 draw { m = bark.Bark(); m = 5; pixel(0, 0, m.base) } })";
    CHECK(compileSource("entry.ips", loadFrom(sources)).status == SourceCompilationStatus::InvalidSource);
}

TEST_CASE("material creation rolls fields in the order they are written") {
    const std::map<std::string, std::string> sources{
        {"entry.ips", R"(import m from "m.ips";
            asset t { size 4x1
              draw { c = m.Coat(); pixel(0, 0, c.base); pixel(1, 0, c.shade); pixel(2, 0, c.edge) }
            })"},
        {"m.ips", "material Coat { base = #000000 to #ffffff; shade = #000000 to #ffffff; "
                  "edge = #000000 to #ffffff }"},
        {"plain.ips", R"(asset t { size 4x1
              draw { a = #000000 to #ffffff; b = #000000 to #ffffff; c = #000000 to #ffffff
                     pixel(0, 0, a); pixel(1, 0, b); pixel(2, 0, c) }
            })"},
    };
    const auto material = compileSource("entry.ips", loadFrom(sources));
    const auto plain = compileSource("plain.ips", loadFrom(sources));
    REQUIRE(material.status == SourceCompilationStatus::Success);
    REQUIRE(plain.status == SourceCompilationStatus::Success);
    for (const iprop::u32 seed : {1u, 7u, 42u})
        CHECK(compiler_test::execute(material.bytes, seed).canvas.pixels ==
              compiler_test::execute(plain.bytes, seed).canvas.pixels);
}

TEST_CASE("top-level palette and const are rejected in an asset file") {
    for (const char* source : {"const f32 K = 2.0\nasset t { size 8x8 draw {} }",
                               "palette P = [#000000]\nasset t { size 8x8 draw {} }"}) {
        INFO(source);
        const auto parsed = ips::parseRecovering(source);
        REQUIRE_FALSE(parsed.diagnostics.empty());
        CHECK(parsed.diagnostics.front().message.find("belong in a material file") != std::string::npos);
    }
}

TEST_CASE("a lowercase return type keeps the parameter type diagnostic") {
    const auto parsed = ips::parseRecovering("asset t { size 8x8 draw {} f(n: i32) -> int { return n } }");
    REQUIRE_FALSE(parsed.diagnostics.empty());
    CHECK(parsed.diagnostics.front().message.find("expected a parameter type") != std::string::npos);
}

TEST_CASE("material file parse errors recover at each top-level declaration") {
    const auto parsed = ips::parseRecovering(
        "const f32 A = )\nmaterial Leaf { base = ; }\npalette Q = )\nconst f32 B = )\n"
        "material Bark { base = #ffffff }");
    CHECK(parsed.diagnostics.size() == 4);
}

namespace {
// Compare executable behavior, including the RNG state after construction,
// against scalar source that does not go through material expansion.
void sameMaterialExecution(const std::map<std::string, std::string>& sources) {
    for (const bool optimize : {false, true}) {
        SourceCompilationOptions options;
        options.optimize = optimize;
        options.compress = false;
        const auto material = compileSource("entry.ips", loadFrom(sources), options);
        const auto plain = compileSource("plain.ips", loadFrom(sources), options);
        INFO("optimize=" << optimize);
        INFO(firstMessage(material));
        INFO(firstMessage(plain));
        REQUIRE(material.succeeded());
        REQUIRE(plain.succeeded());
        for (const u32 seed : {0u, 1u, 7u, 42u, 0xffffffffu}) {
            INFO("seed=" << seed);
            const auto actual = compiler_test::execute(material.bytes, seed);
            const auto expected = compiler_test::execute(plain.bytes, seed);
            CHECK(actual.canvas.pixels == expected.canvas.pixels);
            REQUIRE(actual.outputs.size() == expected.outputs.size());
            for (std::size_t i = 0; i < actual.outputs.size(); ++i) {
                CHECK(actual.outputs[i].name == expected.outputs[i].name);
                REQUIRE(actual.outputs[i].points.size() == expected.outputs[i].points.size());
                for (std::size_t j = 0; j < actual.outputs[i].points.size(); ++j) {
                    CHECK(actual.outputs[i].points[j].x == expected.outputs[i].points[j].x);
                    CHECK(actual.outputs[i].points[j].y == expected.outputs[i].points[j].y);
                }
            }
        }
    }
}
} // namespace

TEST_CASE("material replacement evaluates the RHS before committing any field") {
    sameMaterialExecution({
        {"mat.ips", R"(material Coat {
            base = #000000 to #ffffff
            width = 3
            offset = {1.0, 2.0}
            shade = color.mix(base, #000000, 0.5)
        })"},
        {"entry.ips", R"(import mat from "mat.ips";
            asset t { size 8x1 output point observed draw {
                m = mat.Coat(base = #00ff00)
                copy = m
                m = mat.Coat(width = m.base.r, base = #ff0000, offset = {m.offset.y, m.offset.x})
                observed = {float(m.width), m.offset.x}
                pixel(0, 0, copy.base); pixel(1, 0, m.shade)
                for i in 2..6 {
                    m = mat.Coat(base = rgba(m.base.r - 1, 0, 0, 255), width = m.base.r)
                    pixel(i, 0, rgba(m.width, 0, 0, 255))
                }
                m = m
                paint(m)
                unused = mat.Coat()
                pixel(7, 0, #000000 to #ffffff)
            }
            paint(value: mat.Coat) { local = value; local = mat.Coat(base = value.base); pixel(6, 0, value.base) }
        })"},
        {"plain.ips", R"(asset t { size 8x1 output point observed draw {
            base = #00ff00; width = 3; offset = {1.0, 2.0}
            shade = color.mix(base, #000000, 0.5)
            copy = base
            nextBase = #ff0000; nextWidth = base.r; nextOffset = {offset.y, offset.x}
            nextShade = color.mix(nextBase, #000000, 0.5)
            base = nextBase; width = nextWidth; offset = nextOffset; shade = nextShade
            observed = {float(width), offset.x}
            pixel(0, 0, copy); pixel(1, 0, shade)
            for i in 2..6 {
                nextBase = rgba(base.r - 1, 0, 0, 255); nextWidth = base.r
                nextOffset = {1.0, 2.0}; nextShade = color.mix(nextBase, #000000, 0.5)
                base = nextBase; width = nextWidth; offset = nextOffset; shade = nextShade
                pixel(i, 0, rgba(width, 0, 0, 255))
            }
            pixel(6, 0, base); unused = #000000 to #ffffff; pixel(7, 0, #000000 to #ffffff)
        } })"},
    });
}

TEST_CASE("material bindings retain their nominal type and writable status") {
    const std::string imports = "import mat from \"mat.ips\";\n";
    const std::string definitions = "material Bark { base = #00ff00; width = 3 }\n"
                                    "material Leaf { base = #ff0000; width = 8 }";
    // All these programs passed the old scalar-only check after expansion.
    for (const char* asset : {
        "asset t {size 2x1 draw {m=9; m=mat.Bark()}}",
        "asset t {size 2x1 draw {m=mat.Bark(); m=9}}",
        "asset t {size 2x1 draw {m=mat.Bark(); m=mat.Leaf()}}",
        "asset t {size 2x1 draw {m=mat.Bark(); n=mat.Leaf(); m=n}}",
        "asset t {size 2x1 draw {m=mat.Bark(); if(seed==2){m=mat.Leaf()} paint(m)} paint(m:mat.Leaf){pixel(0,0,m.base)}}",
        "asset t {size 2x1 input i32 m=9 draw {m=mat.Bark()}}",
        "asset t {size 2x1 const i32 m=9 draw {m=mat.Bark()}}",
        "asset t {size 2x1 palette m=[#fff] draw {m=mat.Bark()}}",
        "asset t {size 2x1 output point m draw {m=mat.Bark()}}",
        "asset t {size 2x1 draw {seed=mat.Bark()}}",
        "asset t {size 2x1 draw {m=mat.Bark(); layer m = mat.Bark()}}",
        "asset t {size 2x1 draw {m=mat.Bark(); layer n = m}}",
        "asset t {size 2x1 draw {m=mat.Bark(); for m in 0..2 {}}}",
        "asset t {size 2x1 draw {m=mat.Bark(); layer m {}}}",
        "asset t {size 2x1 draw {} f(m:mat.Bark){m=7}}",
        "asset t {size 2x1 draw {} f(m:mat.Bark){m=mat.Bark()}}",
        "asset t {size 2x1 draw {} f(m:mat.Bark){n=mat.Bark(); m=n}}",
        "asset t {size 2x1 draw {} f(m:i32){m=mat.Bark()}}",
        "asset t {size 2x1 const i32 m=9 draw {} f(m:mat.Bark){}}",
        "asset t {size 2x1 draw {} f(m:i32,m:mat.Bark){}}",
        "asset t {size 2x1 draw {} f(m:mat.Bark,m:mat.Bark){}}",
    }) {
        INFO(asset);
        const std::map<std::string, std::string> sources{{"entry.ips", imports + asset}, {"mat.ips", definitions}};
        const auto result = compileSource("entry.ips", loadFrom(sources));
        CHECK(result.status == SourceCompilationStatus::InvalidSource);
        CHECK_FALSE(result.diagnostics.empty());
        if (result.diagnostics.empty()) continue;
        CHECK(result.bytes.empty());
        CHECK(result.diagnostics.front().stage == ips::DiagnosticStage::Typing);
        CHECK(result.diagnostics.front().path == "entry.ips");
        REQUIRE(result.diagnostics.front().location);
        CHECK(result.diagnostics.front().location->line == 2);
    }
}

TEST_CASE("material fields and private declarations cannot capture source identifiers") {
    sameMaterialExecution({
        {"mat.ips", "const i32 K=3\npalette P=[#00ff00]\nmaterial Bark {base=P[0];width=K}"},
        {"entry.ips", R"(import mat from "mat.ips";
            asset t {size 4x1 const i32 mat__K=99 palette mat__P=[#ff0000] draw {
                m__width=17; m=mat.Bark(); n=m
                paint(m, #0000ff)
                pixel(1,0,rgba(m__width,mat__K,n.width,255)); pixel(2,0,mat__P[0])
                pixel(3,0,#000000 to #ffffff)
            } paint(m:mat.Bark,m__base:color){pixel(0,0,color.mix(m.base,m__base,0.5))} })"},
        {"plain.ips", R"(asset t {size 4x1 draw {
            pixel(0,0,color.mix(#00ff00,#0000ff,0.5))
            pixel(1,0,rgba(17,99,3,255)); pixel(2,0,#ff0000); pixel(3,0,#000000 to #ffffff)
        }})"},
    });
    for (const char* value : {"mat__K", "mat__P[0]", "m__base"}) {
        const std::map<std::string, std::string> sources{
            {"mat.ips", "const i32 K=3\npalette P=[#00ff00]\nmaterial Bark {base=P[0]}"},
            {"entry.ips", std::string("import mat from \"mat.ips\"; asset t {size 1x1 draw {m=mat.Bark(); x=") + value + "}}"},
        };
        const auto result = compileSource("entry.ips", loadFrom(sources));
        REQUIRE(result.status == SourceCompilationStatus::InvalidSource);
        CHECK(firstMessage(result).find(value == std::string("mat__P[0]") ? "mat__P" : value) != std::string::npos);
    }
}

TEST_CASE("qualified materials do not occupy the helper or builtin namespace") {
    sameMaterialExecution({
        {"mat.ips", "material paint {base=#00ff00} material pixel {base=#ff0000} material gaussian {base=#fff}"},
        {"entry.ips", R"(import mat from "mat.ips"; asset t {size 3x1 draw {
            m=mat.paint(); paint(m); n=mat.pixel(); pixel(1,0,n.base)
            pixel(2,0,#000000 to #ffffff with gaussian(0.5,0.18))
        } paint(m:mat.paint){pixel(0,0,m.base)} })"},
        {"plain.ips", R"(asset t {size 3x1 draw {
            pixel(0,0,#00ff00);pixel(1,0,#ff0000)
            pixel(2,0,#000000 to #ffffff with gaussian(0.5,0.18))
        }})"},
    });
}

TEST_CASE("material private declarations and fields have independent import and local scopes") {
    sameMaterialExecution({
        {"a.ips", "const i32 width=123\nmaterial A {base=#00ff00;value=width}"},
        {"b.ips", "material B {base=#ff0000;width=7}"},
        {"entry.ips", R"(import a from "a.ips"; import b from "b.ips";
            asset t {size 3x1 const i32 a__width=11 draw {
                a=b.B(); z=a.A()
                pixel(0,0,rgba(a.width,z.value,a__width,255));pixel(1,0,a.base)
                pixel(2,0,#000000 to #ffffff)
            }})"},
        {"plain.ips", R"(asset t {size 3x1 draw {
            pixel(0,0,rgba(7,123,11,255));pixel(1,0,#ff0000);pixel(2,0,#000000 to #ffffff)
        }})"},
    });
}

TEST_CASE("material expansion preserves independent part streams outputs and helper namespaces") {
    sameMaterialExecution({
        {"mat.ips", "material Coat {base=#000000 to #ffffff with gaussian(0.5,0.18);width=1 to 3}"},
        {"part.ips", R"(import mat from "mat.ips"; asset child {size 4x2 output point p draw {
            m=mat.Coat();old=m;m=mat.Coat(base=m.base,width=m.width+1)
            paint(m);pixel(0,1,old.base);p={float(m.width),float(m.base.r)}
        } paint(m:mat.Coat){line(0,0,m.width,0,1,m.base)}
          gaussian(mean:f32,sigma:f32)->f32{return mean+sigma} })"},
        {"scalar-part.ips", R"(asset child {size 4x2 output point p draw {
            base=#000000 to #ffffff with gaussian(0.5,0.18);width=1 to 3
            old=base;width=width+1
            line(0,0,width,0,1,base);pixel(0,1,old);p={float(width),float(base.r)}
        }})"},
        {"entry.ips", R"(import a from "part.ips"; import b from "part.ips";
            asset t {size 8x3 draw {s=index(1000);a.draw(s,at={0,0});b.draw(s+1,at={4,0});
            pixel(0,2,#000000 to #ffffff)}})"},
        {"plain.ips", R"(import a from "scalar-part.ips"; import b from "scalar-part.ips";
            asset t {size 8x3 draw {s=index(1000);a.draw(s,at={0,0});b.draw(s+1,at={4,0});
            pixel(0,2,#000000 to #ffffff)}})"},
    });
}

TEST_CASE("material defaults retain builtin bindings while overrides use the caller") {
    sameMaterialExecution({
        {"mat.ips", R"(const distribution shape=gaussian(0.5,0.18)
            palette P=[#000000 to #ffffff with beta(2.0,3.0)]
            material Coat {
                base=P[0].pick()
                shade=#000000 to #ffffff with gaussian(0.5,0.18)
                width=1 to 9 with triangular(1.0,4.0,9.0)
                taper=0.0 to 1.0 with shape
            })"},
        {"entry.ips", R"(import mat from "mat.ips"; asset t {size 5x1 output point p draw {
            m=mat.Coat(); n=mat.Coat(taper=gaussian(1.0,2.0))
            pixel(0,0,m.base);pixel(1,0,m.shade);pixel(2,0,rgba(m.width,int(m.taper*255.0),0,255))
            pixel(3,0,n.shade);pixel(4,0,#000000 to #ffffff);p={n.taper,gaussian(3.0,4.0)}
        }
        gaussian(mean:f32,sigma:f32)->f32{return mean+sigma}
        beta(a:f32,b:f32)->f32{return 100.0}
        triangular(a:f32,b:f32,c:f32)->f32{return 100.0}
        })"},
        {"plain.ips", R"(asset t {size 5x1 const distribution shape=gaussian(0.5,0.18)
            palette P=[#000000 to #ffffff with beta(2.0,3.0)] output point p draw {
                base=P[0].pick();shade=#000000 to #ffffff with gaussian(0.5,0.18)
                width=1 to 9 with triangular(1.0,4.0,9.0);taper=0.0 to 1.0 with shape
                nextBase=P[0].pick();nextShade=#000000 to #ffffff with gaussian(0.5,0.18)
                nextWidth=1 to 9 with triangular(1.0,4.0,9.0);nextTaper=3.0
                pixel(0,0,base);pixel(1,0,shade);pixel(2,0,rgba(width,int(taper*255.0),0,255))
                pixel(3,0,nextShade);pixel(4,0,#000000 to #ffffff);p={nextTaper,7.0}
            }})"},
    });
}

TEST_CASE("standalone material authoring checks types without producing an executable") {
    const std::map<std::string, std::string> sources{
        {"mat.ips", "const f32 K=0.5\npalette P=[#000000,#ffffff]\n"
                    "material Coat {base=P[1];taper=K;offset={1,2};width=3}"},
    };
    SourceCompilationOptions options;
    options.allowMaterialEntry = true;
    const auto checked = compileSource("mat.ips", loadFrom(sources), options);
    INFO(firstMessage(checked));
    REQUIRE(checked.succeeded());
    CHECK(checked.bytes.empty());
    REQUIRE(checked.programs.at("mat.ips").isMaterialFile);
    const auto& fields = checked.programs.at("mat.ips").materials.front().fields;
    REQUIRE(fields.size() == 4);
    CHECK(fields[0].type == ips::ParamType::Color);
    CHECK(fields[1].type == ips::ParamType::F32);
    CHECK(fields[2].type == ips::ParamType::Vec2);
    CHECK(fields[3].type == ips::ParamType::I32);
    const auto compiled = compileSource("mat.ips", loadFrom(sources));
    CHECK(compiled.status == SourceCompilationStatus::InvalidSource);
    CHECK(compiled.bytes.empty());
    CHECK(firstMessage(compiled).find("no asset to compile") != std::string::npos);

    for (const char* source : {
        "material Coat {base=1}", "material Coat {base=#fff;bad=missing}",
        "const f32 K=#fff\nmaterial Coat {base=#000}",
        "material Coat {base=#fff;bad=[]}", "material Coat {width=3}",
        "material Coat {base=#fff;base=#000}",
    }) {
        INFO(source);
        const std::map<std::string, std::string> invalid{{"mat.ips", source}};
        const auto result = compileSource("mat.ips", loadFrom(invalid), options);
        CHECK(result.status == SourceCompilationStatus::InvalidSource);
        CHECK(result.bytes.empty());
        REQUIRE_FALSE(result.diagnostics.empty());
        CHECK(result.diagnostics.front().path == "mat.ips");
        CHECK(result.diagnostics.front().stage == ips::DiagnosticStage::Typing);
        CHECK(firstMessage(result).find("no asset to compile") == std::string::npos);
    }
    options.diagnosticLimit = 1;
    const std::map<std::string, std::string> invalid{
        {"mat.ips", "material Coat {base=1;bad=missing;other=unknown}"},
    };
    CHECK(compileSource("mat.ips", loadFrom(invalid), options).diagnostics.size() == 1);
}

TEST_CASE("material file formatting preserves field order comments and executable behavior") {
    const std::string original = R"(// library header
        material Coat { /* base label */ base=P[1]; // base end
            width=K /* width end */;shade=color.mix(base,#000,0.5)
            // closing field
        }
        const i32 K=3; // K end
        palette P=[#0f0, /* palette item */ #f00];
        material Other {base=#fff}
        // library footer
    )";
    const auto formatted = ips::formatProgram(ips::parse(original));
    CHECK(ips::formatProgram(ips::parse(formatted)) == formatted);
    CHECK(formatted.find("material Coat") < formatted.find("const i32 K"));
    CHECK(formatted.find("const i32 K") < formatted.find("palette P"));
    CHECK(formatted.find("palette P") < formatted.find("material Other"));
    CHECK(formatted.find("width =") < formatted.find("shade ="));
    for (const char* comment : {"// library header", "/* base label */", "// base end", "/* width end */",
                                "// closing field", "// K end", "/* palette item */", "// library footer"}) {
        const auto at = formatted.find(comment);
        REQUIRE(at != std::string::npos);
        CHECK(formatted.find(comment, at + 1) == std::string::npos);
    }
    const std::string entry = R"(import mat from "mat.ips"; asset t {size 3x1 draw {
        m=mat.Coat(); pixel(0,0,m.base);pixel(1,0,m.shade);pixel(2,0,rgba(m.width,0,0,255))
    }})";
    std::map<std::string, std::string> sources{{"entry.ips", entry}, {"plain.ips", entry}, {"mat.ips", original}};
    const auto before = compileSource("entry.ips", loadFrom(sources));
    REQUIRE(before.succeeded());
    sources["mat.ips"] = formatted;
    const auto after = compileSource("entry.ips", loadFrom(sources));
    INFO(firstMessage(after));
    REQUIRE(after.succeeded());
    CHECK(before.bytes == after.bytes);
    CHECK(compiler_test::execute(before.bytes, 7).canvas.pixels == compiler_test::execute(after.bytes, 7).canvas.pixels);
}
