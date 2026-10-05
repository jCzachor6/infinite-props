#include <catch2/catch_test_macros.hpp>

#include "Compiler.hpp"
#include "ips/Parser.hpp"
#include "ips/Semantics.hpp"
#include "ips/SourceError.hpp"
#include "support/compiler_support.hpp"
#include "support/semantic_support.hpp"

using namespace iprop;

TEST_CASE("draw entry blocks have implicit runtime indices and typed helper parameters") {
    const std::string source = R"(asset context {
      size 8x8
      variants 3
      archetypes 4
      input u32 offset = 7
      output point context
      draw {
        context = {v + offset, a}
        mark(v, a)
      }
      mark(v: i32, a: i32) { pixel(v, a, #ff00ff) }
    })";
    const auto program = ips::parse(source);
    REQUIRE(program.asset.functions[0].params.empty());
    REQUIRE(program.asset.functions[1].params.size() == 2);
    REQUIRE(program.asset.functions[1].params[0].type == ips::ParamType::I32);
    REQUIRE(program.asset.functions[1].params[1].type == ips::ParamType::I32);
    const auto bytes = compiler_test::compileChecked(source);
    const auto loaded = loadFile(bytes);
    CHECK(loaded.code.entry().params == std::vector{ValueType::I32, ValueType::I32, ValueType::I32});
    for (u32 v = 0; v < 3; ++v) {
        for (u32 a = 0; a < 4; ++a) {
            const auto result = compiler_test::run(bytes, v, a);
            REQUIRE(result.outputs.size() == 1);
            REQUIRE(result.outputs[0].points.size() == 1);
            const auto point = result.outputs[0].points[0];
            CHECK(point.x == static_cast<f32>(v + 7));
            CHECK(point.y == static_cast<f32>(a));
            CHECK(result.canvas.at(v, a) == Color{255, 0, 255, 255});
        }
    }
}

TEST_CASE("legacy draw signatures and return annotations are rejected") {
    for (const std::string header : {"draw()", "draw(v, a)",
            "draw(v: i32, a: i32)", "draw(variant: i32, archetype: i32)",
            "draw -> i32"}) {
        INFO(header);
        CHECK_THROWS_AS(ips::parse("asset t { size 8x8 " + header + " { } }"), ips::SourceError);
    }
    auto program = ips::parse("asset t { size 8x8 draw { } }");
    program.asset.functions[0].params.push_back({"v", ips::ParamType::I32, {1, 1}});
    CHECK_THROWS_AS(ips::checkAsset(program, {}), ips::SourceError);
    CHECK_THROWS_AS(compileProgram(program, {}), ips::SourceError);
}

TEST_CASE("draw context indices cannot be overwritten or shadowed") {
    for (const std::string name : {"v", "a"}) {
        INFO(name);
        semantic_test::expectCheckError(semantic_test::wrap(name + " = 5"), "cannot assign to builtin");
        semantic_test::expectCheckError(semantic_test::wrap("for " + name + " in 0..2 { }"), "reserved name");
        semantic_test::expectCheckError("asset t { size 8x8 output point " + name + " draw { } }", "reserved name");
        semantic_test::expectCheckError("asset t { size 8x8 input u32 " + name + " = 1 draw { } }", "reserved name");
        semantic_test::expectCheckError("asset t { size 8x8 palette " + name + " = [1] draw { } }", "reserved name");
    }
    semantic_test::expectCheckError(semantic_test::wrap("draw()"), "entry block");
    semantic_test::expectCheckError("asset t { size 8x8 draw { helper(v) } helper(n) { } }", "parameter type");
}
