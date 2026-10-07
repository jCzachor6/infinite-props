#include <catch2/catch_test_macros.hpp>

#include "support/compiler_support.hpp"
#include "support/semantic_support.hpp"

using namespace iprop;

TEST_CASE("color namespace helpers preserve mix and shade semantics") {
    const auto bytes = compiler_test::compileChecked(R"(asset colors {
      size 8x8
      input color color = #1496ff
      draw {
        pixel(0, 0, color.shade(color, 10))
        pixel(1, 0, color.shade(color, -256))
        pixel(2, 0, color.mix(#0080ff, #ff8000, 0.5))
        pixel(3, 0, color.mix(color, #ff8000, 0))
        pixel(4, 0, color.mix(color, #ff8000, 1))
      }
    })");
    const auto result = compiler_test::execute(bytes);
    CHECK(result.canvas.at(0, 0) == Color{30, 160, 255, 255});
    CHECK(result.canvas.at(1, 0) == Color{0, 0, 0, 255});
    CHECK(result.canvas.at(2, 0) == Color{127, 128, 127, 255});
    CHECK(result.canvas.at(3, 0) == Color{20, 150, 255, 255});
    CHECK(result.canvas.at(4, 0) == Color{255, 128, 0, 255});
}

TEST_CASE("bare mix and shade are no longer language builtins") {
    semantic_test::expectCheckError(semantic_test::wrap("mix(#ffffff, #000000, 0.5)"),
                                    "undefined function 'mix'");
    semantic_test::expectCheckError(semantic_test::wrap("shade(#ffffff, 1)"),
                                    "undefined function 'shade'");
    semantic_test::expectCheckError(semantic_test::wrap("color.mix(5, #ffffff, 0.5)"),
                                    "must be color, got i32");
}

TEST_CASE("member access on a color or scalar names the value type") {
    semantic_test::expectCheckError(semantic_test::wrap("c = #336699 r = c.r"),
                                    "color channels cannot be read, got '.r'");
    semantic_test::expectCheckError(semantic_test::wrap("n = 3 m = n.x"), "i32 has no components, got '.x'");
    semantic_test::expectCheckError(semantic_test::wrap("p = {1.0, 2.0} w = p.z"),
                                    "vec2 has only .x and .y components, got 'z'");
}
