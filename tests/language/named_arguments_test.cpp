#include <catch2/catch_test_macros.hpp>

#include "support/compiler_support.hpp"
#include "support/semantic_support.hpp"
#include "ips/BuiltinArguments.hpp"
#include "ips/Parser.hpp"

using namespace iprop;
using namespace compiler_test;

TEST_CASE("shared binder represents required optional and named-only parameters") {
    using namespace ips;
    const auto parseCall=[](const std::string& text) {
        return parse("asset t {size 8x8 draw{"+text+"}} ");
    };
    const auto callOf=[](const Program& program)->const Call& {
        return std::get<Call>(*std::get<ExprStmt>(program.asset.functions[0].body[0]).expr);
    };
    const std::vector<ArgumentParameter> parameters{
        {"first"}, {"second"}, {"placement",true,false}, {"override",false,false}
    };
    auto program=parseCall("helper(second=2, 1, placement={0,0})");
    auto bound=bindArguments(callOf(program),parameters,"helper");
    REQUIRE(bound.size()==4);
    CHECK(bound[0]==&callOf(program).args[1]);
    CHECK(bound[1]==&callOf(program).args[0]);
    CHECK(bound[2]==&callOf(program).args[2]);
    CHECK(bound[3]==nullptr);
    for (const auto& source : {
        "helper(1,2)", "helper(1,2,{0,0})", "helper(1,second=2,first=3,placement={0,0})",
        "helper(1,2,unknown=3,placement={0,0})", "helper(1,2,placement={0,0},placement={1,1})"
    }) {
        program=parseCall(source);
        try {
            (void)bindArguments(callOf(program),parameters,"helper");
            FAIL("invalid binding was accepted");
        } catch (const SourceError& error) {
            CHECK(error.code==DiagnosticCode::TypeError);
            CHECK(error.stage==DiagnosticStage::Typing);
        }
    }
}

TEST_CASE("part signature shares the binder without reserving variant or archetype uniforms") {
    using namespace ips;
    const std::vector<std::string> uniforms{"variant","archetype","tint"};
    const auto signature=partParameters(uniforms);
    REQUIRE(signature.size()==10);
    CHECK(signature[1].name=="v");
    CHECK(signature[2].name=="a");
    CHECK(signature[7].name=="variant");
    CHECK(signature[8].name=="archetype");
    const auto program=parse("asset t {size 8x8 draw{child.draw(a=3,seed=1,v=2,at={0,0},variant=4)}}");
    const auto& call=std::get<Call>(*std::get<ExprStmt>(program.asset.functions[0].body[0]).expr);
    const auto args=partArguments(call,uniforms,"child.draw");
    REQUIRE(args.size()==10);
    CHECK(args[0]==&call.args[1]);
    CHECK(args[1]==&call.args[2]);
    CHECK(args[2]==&call.args[0]);
    CHECK(args[3]==&call.args[3]);
    CHECK(args[4]==nullptr);
    CHECK(args[5]==nullptr);
    CHECK(args[6]==nullptr);
    CHECK(args[7]==&call.args[4]);
    CHECK(args[8]==nullptr);
    CHECK(args[9]==nullptr);
}

TEST_CASE("named canvas calls and color shorthand compile the plan example") {
    const auto named = compileChecked("asset t { size 16x16 draw { "
        "rect(color = #fff, h = 10, x = 0, w = 10, y = 0) } }");
    const auto positional = compileChecked("asset t { size 16x16 draw { rect(0, 0, 10, 10, #ffffff) } }");
    CHECK(named == positional);
    CHECK(execute(named).canvas.at(4, 4) == Color{255, 255, 255, 255});
    CHECK(compileChecked("asset t { size 8x8 draw { pixel(0, 0, #aBcD) } }") ==
          compileChecked("asset t { size 8x8 draw { pixel(0, 0, #aaBBccDD) } }"));
    for (const auto color : {"#ff", "#fffff", "#fffffff", "#fffffffff"})
        CHECK_THROWS_AS(compileChecked(semantic_test::wrap("pixel(0, 0, " + std::string(color) + ")")), ips::SourceError);
}

TEST_CASE("variadic polygon choice and palette ramp calls bind named arguments") {
    const auto named = compileChecked(R"(asset t { size 8x8 palette colors = [#f00, #0f0]
      output point p
      draw {
        stream_push(seed = 4)
        p = {pick(value2 = 30, value0 = 10, value1 = 20), chance(p = 0.5)}
        poly(y2 = 4, x1 = 4, color = #fff, y0 = 0, x0 = 0, y1 = 0, x2 = 0)
        pixel(color = ramp(t = 0.5, palette = colors), y = 4, x = 4)
        stream_pop()
      }
    })");
    const auto positional = compileChecked(R"(asset t { size 8x8 palette colors = [#ff0000, #00ff00]
      output point p
      draw {
        stream_push(4)
        p = {pick(10, 20, 30), chance(0.5)}
        poly(#ffffff, 0, 0, 4, 0, 0, 4)
        pixel(4, 4, ramp(colors, 0.5))
        stream_pop()
      }
    })");
    CHECK(named == positional);
}

TEST_CASE("named builtin reordering preserves signature evaluation order") {
    const std::string named = R"(asset t { size 8x8 output list point trace
      draw {
        rect(color = tint(), h = note(4), y = note(2), w = note(3), x = note(1))
        pick(value2 = note(8), value1 = note(7), value0 = note(6))
      }
      note(tag: i32) -> i32 { trace = {tag, unit()} return 0 }
      tint() -> color { trace = {5, unit()} return #ffffff }
    })";
    const std::string positional = R"(asset t { size 8x8 output list point trace
      draw {
        rect(note(1), note(2), note(3), note(4), tint())
        pick(note(6), note(7), note(8))
      }
      note(tag: i32) -> i32 { trace = {tag, unit()} return 0 }
      tint() -> color { trace = {5, unit()} return #ffffff }
    })";
    CHECK(compileChecked(named) == compileChecked(positional));
    const auto points = execute(compileChecked(named)).outputs[0].points;
    REQUIRE(points.size() == 8);
    for (std::size_t i = 0; i < points.size(); ++i) CHECK(points[i].x == i + 1);
}

TEST_CASE("named builtin arguments reject unknown missing duplicate and wrong types") {
    semantic_test::expectCheckError(semantic_test::wrap("rect(x=0, y=0, w=4, h=4, tint=#fff)"), "has no argument 'tint'");
    semantic_test::expectCheckError(semantic_test::wrap("rect(x=0, y=0, w=4, h=4, x=0)"), "duplicate 'x'");
    semantic_test::expectCheckError(semantic_test::wrap("range(1, a=2)"), "duplicate 'a'");
    semantic_test::expectCheckError(semantic_test::wrap("range(a=1)"), "expects 2 arguments");
    semantic_test::expectCheckError(semantic_test::wrap("rect(x=0, y=0, w=4, h=4, color=1)"), "must be color");
    semantic_test::expectCheckError("asset t { size 8x8 palette p=[#fff] draw { ramp(palette=p, value=0.5) } }", "has no argument 'value'");
    CHECK_NOTHROW(compileChecked(semantic_test::wrap("rect(0, 0, color=#fff, h=4, w=4)")));
}
