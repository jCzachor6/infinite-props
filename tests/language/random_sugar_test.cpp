#include <catch2/catch_test_macros.hpp>

#include <bit>

#include "support/compiler_support.hpp"
#include "support/semantic_support.hpp"

using namespace iprop;
using namespace compiler_test;

namespace {
void samePoints(const RunOutcome& actual, const RunOutcome& expected) {
    REQUIRE(actual.outputs.size() == expected.outputs.size());
    for (std::size_t j = 0; j < actual.outputs.size(); ++j) {
        const auto& a = actual.outputs[j].points;
        const auto& b = expected.outputs[j].points;
        REQUIRE(a.size() == b.size());
        for (std::size_t i = 0; i < a.size(); ++i) {
            CHECK(std::bit_cast<u32>(a[i].x) == std::bit_cast<u32>(b[i].x));
            CHECK(std::bit_cast<u32>(a[i].y) == std::bit_cast<u32>(b[i].y));
        }
    }
}
}

TEST_CASE("pick(n) is the exact index alias including zero and unsigned bounds") {
    const std::string source = R"(asset indices { size 8x8 output list point trace
      draw {
        trace = {pick(3), unit()}
        trace = {pick(0), unit()}
        trace = {pick(-1), unit()}
      }
    })";
    auto reference = source;
    while (reference.find("pick(") != std::string::npos)
        reference.replace(reference.find("pick("), 5, "index(");
    const auto bytes = compileChecked(source);
    CHECK(bytes == compileChecked(reference));
    samePoints(execute(bytes), execute(compileChecked(reference)));
}

TEST_CASE("pick selects integers floats colors and vectors with one draw") {
    const auto actual = compileChecked(R"(asset choices { size 8x8 output list point trace
      draw {
        for i in 0..32 {
          trace = {pick(10, 20, 30), unit()}
          trace = {pick(1, 2.5, -0.0), unit()}
          p = pick({1, -0.0}, {2, 3}, {4, 5})
          trace = p
          pixel(i % 8, i / 8, pick(#ff0000, #00ff00, #0000ff))
        }
      }
    })");
    const auto reference = compileChecked(R"(asset choices { size 8x8 output list point trace
      palette ints = [10, 20, 30]
      palette floats = [1.0, 2.5, 0.0]
      palette colors = [#ff0000, #00ff00, #0000ff]
      draw {
        for i in 0..32 {
          trace = {ints[index(3)], unit()}
          floatIndex = index(3)
          floatValue = floats[floatIndex]
          if (floatIndex == 2) { floatValue = -0.0 }
          trace = {floatValue, unit()}
          n = index(3)
          if (n == 0) { trace = {1, -0.0} }
          else if (n == 1) { trace = {2, 3} }
          else { trace = {4, 5} }
          pixel(i % 8, i / 8, colors[index(3)])
        }
      }
    })");
    for (u32 seed : {0u, 1u, 7u, 0xffffffffu}) {
        const auto a = execute(actual, seed);
        const auto b = execute(reference, seed);
        samePoints(a, b);
        CHECK(a.canvas.pixels == b.canvas.pixels);
    }
    semantic_test::expectCheckError(semantic_test::wrap("pick()"), "expects 1 arguments");
    semantic_test::expectCheckError(semantic_test::wrap("pick(#ffffff)"), "must be i32");
    semantic_test::expectCheckError(semantic_test::wrap("pick(1, #ffffff)"), "matching storable types");
    semantic_test::expectCheckError(semantic_test::wrap("pick({1, 2, 3}, {4, 5, 6})"), "matching storable types");
}

TEST_CASE("pick eagerly evaluates all choices once before its selection draw") {
    const auto actual = execute(compileChecked(R"(asset effects { size 8x8
      output list point trace
      draw { value = pick(note(1), note(2), pick(note(3), note(4))) trace = {value, unit()} }
      note(tag: i32) -> i32 { trace = {tag, unit()} return tag }
    })"));
    const auto reference = execute(compileChecked(R"(asset effects { size 8x8
      output list point trace
      palette outer = [1, 2, 0]
      draw {
        for i in 1..5 { trace = {i, unit()} }
        inner = index(2) + 3
        selected = index(3)
        value = inner
        if (selected < 2) { value = outer[selected] }
        trace = {value, unit()}
      }
    })"));
    samePoints(actual, reference);
}

TEST_CASE("chance clamps probabilities by comparison and always consumes a draw") {
    const std::string source = R"(asset probability { size 8x8 output list point trace
      draw {
        trace = {chance(0), unit()}
        trace = {chance(1), unit()}
        trace = {chance(-2.0), unit()}
        trace = {chance(2.0), unit()}
        trace = {chance(pow(-1, 0.5)), unit()}
        trace = {chance(0.5), unit()}
        trace = {chance(unit()), unit()}
      }
    })";
    const auto bytes = compileChecked(source);
    const auto reference = compileChecked(R"(asset probability { size 8x8 output list point trace
      draw {
        trace = {0.0 > unit(), unit()}
        trace = {1.0 > unit(), unit()}
        trace = {-2.0 > unit(), unit()}
        trace = {2.0 > unit(), unit()}
        trace = {pow(-1, 0.5) > unit(), unit()}
        trace = {0.5 > unit(), unit()}
        trace = {unit() > unit(), unit()}
      }
    })");
    for (u32 seed : {0u, 1u, 26u, 0xffffffffu}) {
        const auto actual = execute(bytes, seed);
        samePoints(actual, execute(reference, seed));
        const auto& points = actual.outputs[0].points;
        CHECK(points[0].x == 0);
        CHECK(points[1].x == 1);
        CHECK(points[2].x == 0);
        CHECK(points[3].x == 1);
        CHECK(points[4].x == 0);
    }
}

TEST_CASE("oversized choice lists report a located compiler resource error") {
    std::string choices = "1";
    for (std::size_t i = 1; i < kMaxLocals; ++i) choices += ", 1";
    CHECK_THROWS_AS(compileChecked(semantic_test::wrap("value = pick(" + choices + ")")), ips::SourceError);
}
