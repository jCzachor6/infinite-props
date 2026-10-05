#include <catch2/catch_test_macros.hpp>

#include <bit>

#include "support/compiler_support.hpp"
#include "support/semantic_support.hpp"

using namespace iprop;
using namespace compiler_test;

namespace {
void sameResult(const RunOutcome& actual, const RunOutcome& reference) {
    CHECK(actual.canvas.pixels == reference.canvas.pixels);
    REQUIRE(actual.outputs.size() == reference.outputs.size());
    for (std::size_t j = 0; j < actual.outputs.size(); ++j) {
        const auto& a = actual.outputs[j].points;
        const auto& b = reference.outputs[j].points;
        REQUIRE(a.size() == b.size());
        for (std::size_t i = 0; i < a.size(); ++i) {
            CHECK(std::bit_cast<u32>(a[i].x) == std::bit_cast<u32>(b[i].x));
            CHECK(std::bit_cast<u32>(a[i].y) == std::bit_cast<u32>(b[i].y));
        }
    }
}
}

TEST_CASE("seed-direct draws match manual isolation and preserve the outer stream") {
    const auto actual = compileChecked(R"(asset direct { size 8x8 output list point trace
      draw {
        for i in 0..32 {
          trace = {unit(), roll(123, -2, 8)}
          trace = {roll(123, -2, 8), unit()}
          trace = {choose(7, 10, 20, 30), unit()}
          pixel(i % 8, i / 8, choose(-1, #f00, #0f0, #00f))
          trace = choose(99, {1, -0.0}, {2, 3})
          trace = {choose(0, 42), unit()}
        }
      }
    })");
    const auto reference = compileChecked(R"(asset direct { size 8x8 output list point trace
      palette numbers = [10, 20, 30]
      palette colors = [#f00, #0f0, #00f]
      draw {
        for i in 0..32 {
          before = unit()
          stream_push(123) r = range(-2, 8) stream_pop()
          trace = {before, r}
          stream_push(123) r = range(-2, 8) stream_pop()
          trace = {r, unit()}
          stream_push(7) n = index(3) stream_pop()
          trace = {numbers[n], unit()}
          stream_push(-1) n = index(3) stream_pop()
          pixel(i % 8, i / 8, colors[n])
          stream_push(99) n = index(2) stream_pop()
          if (n == 0) { trace = {1, -0.0} } else { trace = {2, 3} }
          stream_push(0) index(1) stream_pop()
          trace = {42, unit()}
        }
      }
    })");
    for (u32 seed : {0u, 1u, 26u, 0x80000000u, 0xffffffffu})
        sameResult(execute(actual, seed), execute(reference, seed));
}

TEST_CASE("seed and choices evaluate before isolation in signature order") {
    const auto actual = compileChecked(R"(asset effects { size 8x8 output list point trace
      draw {
        value = roll(b = note(3), seed = note(1), a = note(2))
        trace = {value, unit()}
        picked = choose(value1 = note(6), seed = note(4), value0 = note(5))
        trace = {picked, unit()}
      }
      note(tag: i32) -> i32 { trace = {tag, unit()} return tag }
    })");
    const auto reference = compileChecked(R"(asset effects { size 8x8 output list point trace
      draw {
        s = note(1) low = note(2) high = note(3)
        stream_push(s) value = range(low, high) stream_pop()
        trace = {value, unit()}
        s = note(4) first = note(5) second = note(6)
        stream_push(s) n = index(2) stream_pop()
        picked = first
        if (n == 1) { picked = second }
        trace = {picked, unit()}
      }
      note(tag: i32) -> i32 { trace = {tag, unit()} return tag }
    })");
    for (u32 seed : {0u, 7u, 0xffffffffu})
        sameResult(execute(actual, seed), execute(reference, seed));
}

TEST_CASE("seed-direct draws nest and accept numeric mixed choices") {
    const std::string named = R"(asset nesting { size 8x8 output list point trace
      draw {
        stream_push(77)
        trace = {roll(b = roll(9, 2, 3), seed = choose(7, 10, 20), a = roll(5, -1, 1)), unit()}
        trace = {choose(value1 = 2.5, seed = seed, value0 = 1), unit()}
        stream_pop()
        trace = {unit(), choose(value0 = 8, seed = 3)}
      }
    })";
    const std::string positional = R"(asset nesting { size 8x8 output list point trace
      draw {
        stream_push(77)
        trace = {roll(choose(7, 10, 20), roll(5, -1, 1), roll(9, 2, 3)), unit()}
        trace = {choose(seed, 1, 2.5), unit()}
        stream_pop()
        trace = {unit(), choose(3, 8)}
      }
    })";
    CHECK(compileChecked(named) == compileChecked(positional));
    CHECK_NOTHROW(execute(compileChecked(named)));
    semantic_test::expectCheckError(semantic_test::wrap("roll(1.0, 0, 1)"), "must be i32");
    semantic_test::expectCheckError(semantic_test::wrap("choose(1.0, 10, 20)"), "seed must be i32");
    semantic_test::expectCheckError(semantic_test::wrap("choose(1)"), "at least one choice");
    semantic_test::expectCheckError(semantic_test::wrap("choose(1, 2, #fff)"), "matching storable types");
    semantic_test::expectCheckError(semantic_test::wrap("choose(seed=1, typo=2)"), "has no argument 'typo'");
}
