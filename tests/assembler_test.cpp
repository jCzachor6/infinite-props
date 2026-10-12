#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <string>
#include <bit>
#include <cstdio>

#include "support/test_support.hpp"

#include "ipa.hpp"
#include "ipdis.hpp"

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
        endfn
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
        endfn
    )";
    const auto bytes = assemble(source);
    auto loaded = loadFile(bytes);
    REQUIRE(loaded.pool.ints.size() == 1);
    REQUIRE(loaded.pool.floats.size() == 1);
    REQUIRE(loaded.pool.ints[0] == 1);
}

TEST_CASE("assembly preserves subnormal and nonfinite float encodings losslessly") {
    const std::vector<u32> encodings{0, 0x80000000, 1, 0x80000001, 0x007fffff, 0x00800000,
        0x7f7fffff, 0x7f800000, 0xff800000, 0x7fc00001, 0xffc12345, 0x7fa00001};
    std::string source = "meta: size=1x1 origin=bits:0x00000001,bits:0x80000001\n"
        "input: f32 tiny = bits:0x00000001\nfn draw ( i32, i32, f32 ) -> ( ):\n";
    for (const auto bits : encodings) {
        char text[32];
        std::snprintf(text, sizeof(text), "bits:0x%08x", bits);
        source += "const.f32 " + std::string(text) + "\ndrop\n";
    }
    source += "endfn\n";
    const auto binary = assemble(source);
    const auto text = disassemble(binary);
    CHECK(text.find("bits:0x00000001") != std::string::npos);
    CHECK(text.find("bits:0x7fc00001") != std::string::npos);
    CHECK(text.find("bits:0x7fa00001") != std::string::npos);
    const auto restored = assemble(text);
    CHECK(disassemble(restored) == text);
    const auto loaded = loadFile(restored);
    REQUIRE(loaded.pool.floats.size() == encodings.size());
    for (std::size_t i = 0; i < encodings.size(); ++i)
        CHECK(std::bit_cast<u32>(loaded.pool.floats[i]) == encodings[i]);
    CHECK(std::bit_cast<u32>(loaded.meta.origin.x) == 1);
    CHECK(std::bit_cast<u32>(loaded.meta.origin.y) == 0x80000001);
    CHECK(std::bit_cast<u32>(loaded.meta.inputs[0].f32Default) == 1);
    for (const auto invalid : {"bits:0x1", "bits:0x100000000", "bits:0xggffffff", "bits:0x00000001junk"})
        CHECK_THROWS_WITH(assemble("meta: size=1x1\nfn draw ( i32, i32 ) -> ( ):\nconst.f32 " +
            std::string(invalid) + "\ndrop\nendfn\n"), Catch::Matchers::ContainsSubstring("bad float literal"));
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
        endfn
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
        endfn
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
          endfn
        endpart
        fn paint ( ) -> ( ):
          return
        endfn
        fn draw ( i32, i32 ) -> ( ):
          const.i32 0
          const.i32 0
          const.i32 0
          const.f32 0.0
          const.f32 0.0
          part.draw stamp
          call paint
          return
        endfn
    )";
    const auto result = runSource(source);
    REQUIRE(result.image.at(0, 0) == Color{0, 0, 255, 255});
}

TEST_CASE("assembler errors carry the offending line") {
    for (const std::string kind : {"-1", "8", "256", "1.5", "nope"}) {
        REQUIRE_THROWS_WITH(assemble("meta: size=8x8\nfn draw ( i32, i32 ) -> ( ):\n"
                                    "part.draw stamp " + kind + "\nendfn\n"),
                           Catch::Matchers::ContainsSubstring("part transform kind"));
    }
    REQUIRE_THROWS_WITH(assemble(R"(
        meta: size=8x8
        fn draw ( i32, i32 ) -> ( ):
          bogus.op 1
          return
        endfn
    )"),
                        Catch::Matchers::ContainsSubstring("line 4"));
    REQUIRE_THROWS_WITH(assemble(R"(
        meta: size=8x8
        fn draw ( i32, i32 ) -> ( ):
          const.i32 nope
          return
        endfn
    )"),
                        Catch::Matchers::ContainsSubstring("bad integer"));
    REQUIRE_THROWS_WITH(assemble(R"(
        meta: size=8x8
        fn draw ( i32, i32 ) -> ( ):
          out.point missing
          return
        endfn
    )"),
                        Catch::Matchers::ContainsSubstring("unknown output slot"));
}

TEST_CASE("the first function must be named draw") {
    REQUIRE_THROWS_WITH(assemble(R"(
        meta: size=8x8
        fn main ( i32, i32 ) -> ( ):
          return
        endfn
    )"),
                        Catch::Matchers::ContainsSubstring("draw"));
}
