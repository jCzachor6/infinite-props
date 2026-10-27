#include <catch2/catch_test_macros.hpp>

#include "SourceCompilation.hpp"
#include "iprop/VM.hpp"
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
