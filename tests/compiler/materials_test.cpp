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
            m = Bark()
            branch(8.0, 15.0, 5.0, m)
            pixel(1, 1, m.shadow)
          }
          branch(x: f32, y: f32, w: f32, m: Bark) {
            line(x, y, x, y - w, int(m.edge_width) + 1, m.base)
            pixel(int(x), int(y - w) - 1, m.edge)
          }
        })"},
    {"bark.ips", R"(material Bark {
          base = #5a4230 to #7a5c40
          shadow = #2a1a10
          highlight = #9a7a54
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
        asset tree { size 16x16 draw { m = Bark(); pixel(1, 1, m.missing); } })";
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
    CHECK(formatted.find("m = Bark()") != std::string::npos);
    CHECK(formatted.find("m: Bark") != std::string::npos);
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
              draw { m = Bark(); )") + call + R"( }
              branch(x: f32, y: f32, w: f32, m: Bark) { pixel(int(x), int(y - w), m.base) }
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
              draw { m = Bark(); n = Leaf(); paint(m); pixel(1, 1, n.base); pixel(2, 2, SHADES[0]) }
              paint(m: Bark) { pixel(0, 0, m.base); pixel(int(m.bend * K), 0, m.base) }
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

    sources["bark.ips"] = "material Bark { base = #5a4230; taper = #ffffff }";
    result = compileSource("entry.ips", loadFrom(sources));
    REQUIRE_FALSE(result.diagnostics.empty());
    CHECK(result.diagnostics.front().path == "bark.ips");
    CHECK(result.diagnostics.front().location->line == 1);
    CHECK(firstMessage(result).find("'taper' must be f32") != std::string::npos);
}

TEST_CASE("two imported materials with the same name are rejected at the import") {
    const std::map<std::string, std::string> sources{
        {"entry.ips", "import a from \"a.ips\";\nimport b from \"b.ips\";\n"
                      "asset t { size 8x8 draw { m = Bark(); pixel(0, 0, m.base) } }"},
        {"a.ips", "material Bark { base = #000000 }"},
        {"b.ips", "material Bark { base = #ffffff }"},
    };
    const auto result = compileSource("entry.ips", loadFrom(sources));
    REQUIRE(result.status == SourceCompilationStatus::InvalidSource);
    CHECK(result.diagnostics.front().path == "entry.ips");
    CHECK(result.diagnostics.front().location->line == 2);
}

TEST_CASE("material creation rolls fields in the order they are written") {
    const std::map<std::string, std::string> sources{
        {"entry.ips", R"(import m from "m.ips";
            asset t { size 4x1
              draw { c = Coat(); pixel(0, 0, c.base); pixel(1, 0, c.shade); pixel(2, 0, c.edge) }
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
