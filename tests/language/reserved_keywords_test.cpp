#include <catch2/catch_test_macros.hpp>
#include "SourceCompilation.hpp"
#include "ips/Lexer.hpp"
#include "ips/Parser.hpp"
#include "ips/Printer.hpp"

#include <array>

using namespace iprop;

TEST_CASE("new keywords are reserved in every name position", "[keywords]") {
    for (const std::string word : {"const", "to", "with", "distribution", "sampler", "stepper", "weighted", "density", "layer", "mask", "clip"}) {
        INFO(word);
        const auto tokens = ips::lex(word + " " + word + "Count");
        CHECK(tokens[0].kind != ips::TokenKind::Identifier);
        CHECK(tokens[1].kind == ips::TokenKind::Identifier);
        const std::array sources{
            "asset " + word + "{size 1x1 draw{}}",
            "import " + word + " from \"child.ips\" asset t{size 1x1 draw{}}",
            "asset t{size 1x1 input i32 " + word + "=1 draw{}}",
            "asset t{size 1x1 output point " + word + " draw{}}",
            "asset t{size 1x1 output mask " + word + " draw{}}",
            "asset t{size 1x1 output layer " + word + " draw{}}",
            "asset t{size 1x1 const i32 " + word + "=1 draw{}}",
            "asset t{size 1x1 palette " + word + "=[#fff] draw{}}",
            "asset t{size 1x1 " + word + "(x:i32)->i32{return x} draw{}}",
            "asset t{size 1x1 echo(" + word + ":i32){} draw{}}",
            "asset t{size 1x1 draw{" + word + "=1}}",
            "asset t{size 1x1 draw{for " + word + " in 0..2{}}}",
            "asset t{size 1x1 draw{layer " + word + "{}}}",
            "asset t{size 1x1 draw{x=child." + word + "}}",
        };
        for (const auto& source : sources) {
            INFO(source);
            const auto result = compileSource("keyword.ips", [&](std::string_view) { return source; });
            CHECK_FALSE(result.succeeded()); CHECK(result.bytes.empty());
            REQUIRE_FALSE(result.diagnostics.empty());
            CHECK(result.diagnostics.front().stage == ips::DiagnosticStage::Parser);
            REQUIRE(result.diagnostics.front().location.has_value());
            CHECK(result.diagnostics.front().location->line >= 1);
            CHECK(result.diagnostics.front().location->column >= 1);
            CHECK(result.diagnostics.front().range.has_value());
        }
    }
}

TEST_CASE("reserved keyword syntax still formats and compiles without contextual lookahead", "[keywords]") {
    const std::string source = R"(asset t{size 2x2
      const distribution mass=weighted{1:1,2:1}
      const sampler<i32> drawCount=sampler(1 to 2 with mass)
      echo(image:layer)->layer{return image}
      draw{distribution shape=density{0:0,1:1,2:0}
        sampler<f32> height=sampler(0.0 to 2.0 with shape)
        layer image(alpha=/* alpha */surface.source_over){pixel(0,0,#11223380)}
        layer copy=echo(image) surface.paint(copy,blend=surface.source_over)
        x=height.pick();n=drawCount.pick()}
    })";
    const auto formatted = ips::formatProgram(ips::parse(source));
    CHECK(ips::formatProgram(ips::parse(formatted)) == formatted);
    CHECK(formatted.find("/* alpha */") != std::string::npos);
    for (const auto& text : {source,formatted})
        CHECK(compileSource("keywords.ips", [&](std::string_view) { return text; }).succeeded());
}
