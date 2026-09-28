#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <string>

#include "support/test_support.hpp"

#include "ipa.hpp"

#include "iprop/VM.hpp"

// The .ipa text assembler: correct emission, constant dedup, named slots
// and parts, and error reporting.

using namespace iprop;
using namespace test_support;

namespace {

// Assembles and runs; returns the generation result.
GenerationResult runSource(const std::string& source, u32 seed = 0) {
    const auto bytes = assemble(source);
    auto loaded = loadFile(bytes);
    validate(loaded);
    VM vm(loaded, {seed, 0, 0, 0, 0, {}});
    vm.run();
    return vm.result();
}

} // namespace

TEST_CASE("a minimal program assembles, validates and runs") {
    const std::string source = R"(
        meta: size=8x8 variants=1 archetypes=1 fuel=1000
        fn draw ( i32, i32 ) -> ( ):
          const.i32 1
          const.i32 1
          const.color #ff0000ff
          pixel
          return
        end
    )";
    const auto result = runSource(source);
    REQUIRE(result.image.at(1, 1) == Color{255, 0, 0, 255});
}

TEST_CASE("constants are deduplicated into the pools") {
    const std::string source = R"(
        meta: size=8x8 variants=1 archetypes=1 fuel=1000
        fn draw ( i32, i32 ) -> ( ):
          const.i32 1
          const.i32 1
          const.i32 1
          const.f32 2.5
          const.f32 2.5
          drop
          drop
          drop
          drop
          drop
          return
        end
    )";
    const auto bytes = assemble(source);
    auto loaded = loadFile(bytes);
    REQUIRE(loaded.pool.ints.size() == 1);
    REQUIRE(loaded.pool.floats.size() == 1);
    REQUIRE(loaded.pool.ints[0] == 1);
}

TEST_CASE("negative and hex integer literals keep their bit patterns") {
    const std::string source = R"(
        meta: size=8x8 variants=1 archetypes=1 fuel=1000
        fn draw ( i32, i32 ) -> ( ):
          const.i32 -1
          const.i32 0xffffffff
          drop
          drop
          return
        end
    )";
    const auto bytes = assemble(source);
    auto loaded = loadFile(bytes);
    REQUIRE(loaded.pool.ints.size() == 1);
    REQUIRE(loaded.pool.ints[0] == -1);
}

TEST_CASE("named output slots resolve to global indices") {
    const std::string source = R"(
        meta: size=8x8 variants=1 archetypes=1 fuel=1000
        output: point anchor
        output: list rect colliders
        fn draw ( i32, i32 ) -> ( ):
          const.f32 1.0
          const.f32 2.0
          out.point anchor
          const.f32 0.0
          const.f32 0.0
          const.f32 4.0
          const.f32 4.0
          out.list.rect colliders
          return
        end
    )";
    const auto result = runSource(source);
    REQUIRE(result.outputs.size() == 2);
    REQUIRE(result.outputs[0].name == "anchor");
    REQUIRE(result.outputs[0].points[0].x == 1.f);
    REQUIRE(result.outputs[0].points[0].y == 2.f);
    REQUIRE(result.outputs[1].name == "colliders");
    REQUIRE(result.outputs[1].isList);
    REQUIRE(result.outputs[1].rects.size() == 1);
}

TEST_CASE("helpers and parts assemble with named references") {
    const std::string source = R"(
        meta: size=8x8 variants=1 archetypes=1 fuel=100000
        part stamp:
          meta: size=8x8 origin=0,0
          fn draw ( i32, i32 ) -> ( ):
            const.i32 0
            const.i32 0
            const.color #0000ffff
            pixel
            return
          end
        end
        fn paint ( ) -> ( ):
          return
        end
        fn draw ( i32, i32 ) -> ( ):
          const.i32 0
          const.i32 0
          const.i32 0
          const.f32 0.0
          const.f32 0.0
          part.draw stamp
          call paint
          return
        end
    )";
    const auto result = runSource(source);
    REQUIRE(result.image.at(0, 0) == Color{0, 0, 255, 255});
}

TEST_CASE("assembler errors carry the offending line") {
    REQUIRE_THROWS_WITH(assemble(R"(
        meta: size=8x8
        fn draw ( i32, i32 ) -> ( ):
          bogus.op 1
          return
        end
    )"),
                        Catch::Matchers::ContainsSubstring("line 4"));
    REQUIRE_THROWS_WITH(assemble(R"(
        meta: size=8x8
        fn draw ( i32, i32 ) -> ( ):
          const.i32 nope
          return
        end
    )"),
                        Catch::Matchers::ContainsSubstring("bad integer"));
    REQUIRE_THROWS_WITH(assemble(R"(
        meta: size=8x8
        fn draw ( i32, i32 ) -> ( ):
          out.point missing
          return
        end
    )"),
                        Catch::Matchers::ContainsSubstring("unknown output slot"));
}

TEST_CASE("the first function must be named draw") {
    REQUIRE_THROWS_WITH(assemble(R"(
        meta: size=8x8
        fn main ( i32, i32 ) -> ( ):
          return
        end
    )"),
                        Catch::Matchers::ContainsSubstring("draw"));
}
