#include <catch2/catch_test_macros.hpp>

#include <bit>

#include "support/compiler_support.hpp"
#include "support/semantic_support.hpp"

using namespace iprop;
using namespace compiler_test;

TEST_CASE("conventional numeric casts preserve identity and pinned truncation") {
    const auto bytes = compileChecked(R"(asset casts {
      size 8x8
      output list point values
      draw {
        values = {int(16777217) - 16777216, float(-12)}
        values = {int(-2.9), int(2.9)}
        values = {int(pow(0, -1)), int(-pow(0, -1))}
        values = {int(pow(-1, 0.5)), float(0.5)}
      }
    })");
    const auto points = execute(bytes).outputs[0].points;
    REQUIRE(points.size() == 4);
    CHECK(points[0].x == 1);
    CHECK(points[0].y == -12);
    CHECK(points[1].x == -2);
    CHECK(points[1].y == 2);
    CHECK(points[2].x == static_cast<f32>(2147483647));
    CHECK(points[2].y == static_cast<f32>(-2147483647 - 1));
    CHECK(points[3].x == 0);
    CHECK(points[3].y == 0.5f);
    semantic_test::expectCheckError(semantic_test::wrap("int(#ffffff)"), "must be i32");
    semantic_test::expectCheckError(semantic_test::wrap("float({1, 2})"), "must be f32");
}

TEST_CASE("lerp chooses color or f32 overloads without changing their bytecode") {
    const std::string original = R"(asset interpolation { size 8x8
      output point value
      draw {
        value = {lerp(1, 5, 0.25), lerp(1.0, 5.0, 0.25)}
        pixel(0, 0, color.mix(#0080ff, #ff8000, 0.5))
      }
    })";
    auto alias = original;
    alias.replace(alias.find("color.mix"), 9, "lerp");
    const auto bytes = compileChecked(alias);
    CHECK(bytes == compileChecked(original));
    const auto result = execute(bytes);
    CHECK(result.outputs[0].points[0].x == 2);
    CHECK(result.outputs[0].points[0].y == 2);
    CHECK(result.canvas.at(0, 0) == Color{127, 128, 127, 255});
    semantic_test::expectCheckError(semantic_test::wrap("lerp(#ffffff, 1, 0.5)"), "must be f32");
}

TEST_CASE("rgb and rgba build exact byte channels and clamp all integers") {
    const auto bytes = compileChecked(R"(asset channels {
      size 256x3
      draw {
        for i in 0..256 {
          pixel(i, 0, rgb(i, 255 - i, (i * 17) % 256))
          pixel(i, 1, #ffffff)
          pixel(i, 1, rgba(i, 255 - i, (i * 17) % 256, i))
        }
        pixel(0, 2, rgb(-2147483647 - 1, 2147483647, -1))
        pixel(1, 2, #ffffff)
        pixel(1, 2, rgba(0, 0, 0, -2147483647 - 1))
        pixel(2, 2, rgba(1, 2, 3, 2147483647))
      }
    })");
    const auto canvas = execute(bytes).canvas;
    for (u32 i = 0; i < 256; ++i) {
        const u32 channels[] = {i, 255 - i, (i * 17) % 256};
        CHECK(canvas.at(i, 0) == Color{static_cast<u8>(channels[0]),
              static_cast<u8>(channels[1]), static_cast<u8>(channels[2]), 255});
        const auto blend = [i](u32 c) { return static_cast<u8>((c * i + 255 * (255 - i)) / 255); };
        CHECK(canvas.at(i, 1) == Color{blend(channels[0]), blend(channels[1]), blend(channels[2]), 255});
    }
    CHECK(canvas.at(0, 2) == Color{0, 255, 0, 255});
    CHECK(canvas.at(1, 2) == Color{255, 255, 255, 255});
    CHECK(canvas.at(2, 2) == Color{1, 2, 3, 255});
    semantic_test::expectCheckError(semantic_test::wrap("rgb(1.5, 2, 3)"), "must be i32");
    semantic_test::expectCheckError(semantic_test::wrap("rgba(1, 2, 3)"), "expects 4 arguments");
}

TEST_CASE("color constructor arguments execute once in source order") {
    const auto actual = execute(compileChecked(R"(asset effects { size 8x8
      output list point trace
      draw { pixel(0, 0, rgba(channel(1), channel(2), channel(3), channel(4))) }
      channel(tag: i32) -> i32 { trace = {tag, unit()} return tag }
    })"));
    const auto reference = execute(compileChecked(R"(asset effects { size 8x8
      output list point trace
      draw { for i in 1..5 { trace = {i, unit()} } }
    })"));
    REQUIRE(actual.outputs[0].points.size() == 4);
    for (std::size_t i = 0; i < 4; ++i) {
        CHECK(actual.outputs[0].points[i].x == reference.outputs[0].points[i].x);
        CHECK(std::bit_cast<u32>(actual.outputs[0].points[i].y) ==
              std::bit_cast<u32>(reference.outputs[0].points[i].y));
    }
}
