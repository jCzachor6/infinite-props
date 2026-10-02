#include <catch2/catch_test_macros.hpp>

#include "support/compiler_support.hpp"
#include "support/semantic_support.hpp"

using namespace iprop;
using namespace compiler_test;

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
