#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <bit>
#include <map>
#include <string>
#include <vector>

#include "Compiler.hpp"
#include "iprop/VM.hpp"
#include "ips/Parser.hpp"
#include "ips/Semantics.hpp"
#include "ips/SourceError.hpp"
#include "ipa.hpp"
#include "ipdis.hpp"

#include "support/compiler_support.hpp"

using namespace iprop;
using namespace compiler_test;

TEST_CASE("part transforms mirror the pixels and propagate the outputs") {
    const std::string wing = R"(
asset wing {
  size 4x4
  output point tip
  draw(v: i32, a: i32) {
    pixel(0, 0, #ff0000)
    pixel(1, 1, #00ff00)
    tip = {1, 0}
  }
})";
    const std::string composite = R"(
import wing from "wing.ips"
asset t {
  size 8x8
  output point p
  draw(v: i32, a: i32) {
    wing.draw(1, v, a, at = {0, 0})
    wing.draw(2, v, a, at = {4, 0}, mirror_x = true)
    p = wing.outputs.tip
  }
})";
    const ips::Program wingProgram = ips::parse(wing);
    const ips::Program compositeProgram = ips::parse(composite);
    ips::checkAsset(wingProgram, {});
    ips::checkAsset(compositeProgram, {{"wing", partInfoOf(wingProgram.asset)}});
    const auto bytes = compileProgram(compositeProgram, {{"wing", &wingProgram}});
    const auto loaded = loadFile(bytes);
    CHECK_NOTHROW(validate(loaded));
    VM vm(loaded, GenerationInputs{5, 0, 0, 0, 0, {}});
    CHECK_NOTHROW(vm.run());
    const GenerationResult result = vm.result();
    // The identity blit paints (0,0) and (1,1).
    REQUIRE(result.image.at(0, 0) == Color{255, 0, 0, 255});
    REQUIRE(result.image.at(1, 1) == Color{0, 255, 0, 255});
    // The mirrored blit at (4,0): (0,0) -> (7,0), (1,1) -> (6,1).
    REQUIRE(result.image.at(7, 0) == Color{255, 0, 0, 255});
    REQUIRE(result.image.at(6, 1) == Color{0, 255, 0, 255});
    REQUIRE(result.image.at(0, 0) == Color{255, 0, 0, 255});
    // The output propagates through the mirror: {1, 0} -> {3, 0}.
    const NamedOutput* p = result.find("p");
    REQUIRE(p != nullptr);
    REQUIRE(p->points.size() == 1);
    REQUIRE(p->points[0].x == 3.f);
    REQUIRE(p->points[0].y == 0.f);
}

TEST_CASE("the rot and mirror compositions fold to the D4 kinds") {
    const std::string wing = R"(
asset wing {
  size 4x4
  draw(v: i32, a: i32) {
    pixel(0, 0, #ff0000)
  }
})";
    // rot 180 with both mirrors is the identity: the pixel stays at (0,0).
    const std::string composite = R"(
import wing from "wing.ips"
asset t {
  size 4x4
  draw(v: i32, a: i32) {
    wing.draw(1, v, a, at = {0, 0}, rot = 180, mirror_x = true, mirror_y = true)
  }
})";
    const ips::Program wingProgram = ips::parse(wing);
    const ips::Program compositeProgram = ips::parse(composite);
    ips::checkAsset(wingProgram, {});
    ips::checkAsset(compositeProgram, {{"wing", partInfoOf(wingProgram.asset)}});
    const auto bytes = compileProgram(compositeProgram, {{"wing", &wingProgram}});
    const auto loaded = loadFile(bytes);
    CHECK_NOTHROW(validate(loaded));
    VM vm(loaded, GenerationInputs{5, 0, 0, 0, 0, {}});
    CHECK_NOTHROW(vm.run());
    REQUIRE(vm.result().image.at(0, 0) == Color{255, 0, 0, 255});

    // The rot 90 of the 4x4 part lands (0,0) at (0,3) — the (dx, dy) =
    // (sy, w-1-sx) mapping (spec §10.5, y grows downward).
    const std::string rotated = R"(
import wing from "wing.ips"
asset t {
  size 4x4
  draw(v: i32, a: i32) {
    wing.draw(1, v, a, at = {0, 0}, rot = 90)
  }
})";
    const ips::Program rotatedProgram = ips::parse(rotated);
    ips::checkAsset(rotatedProgram, {{"wing", partInfoOf(wingProgram.asset)}});
    const auto rotatedBytes =
        compileProgram(rotatedProgram, {{"wing", &wingProgram}});
    const auto rotatedLoaded = loadFile(rotatedBytes);
    CHECK_NOTHROW(validate(rotatedLoaded));
    VM rotatedVm(rotatedLoaded, GenerationInputs{5, 0, 0, 0, 0, {}});
    CHECK_NOTHROW(rotatedVm.run());
    REQUIRE(rotatedVm.result().image.at(0, 3) == Color{255, 0, 0, 255});
}

TEST_CASE("part transform values are checked at the compile") {
    const std::string wing = R"(
asset wing {
  size 4x4
  draw(v: i32, a: i32) {
    pixel(0, 0, #ff0000)
  }
})";
    const ips::Program wingProgram = ips::parse(wing);
    const auto compileComposite = [&](const std::string& source,
                                      const char* messagePart) {
        bool threw = false;
        try {
            const ips::Program program = ips::parse(source);
            ips::checkAsset(program, {{"wing", partInfoOf(wingProgram.asset)}});
            (void)compileProgram(program, {{"wing", &wingProgram}});
        } catch (const ips::SourceError& error) {
            threw = true;
            REQUIRE(std::string(error.message).find(messagePart) !=
                    std::string::npos);
        }
        REQUIRE(threw);
    };
    compileComposite(
        "import wing from \"wing.ips\"\nasset t { size 4x4 "
        "draw(v: i32, a: i32) { wing.draw(1, v, a, at = {0, 0}, rot = 45) } }",
        "the rot transform must be 0, 90, 180 or 270");
    compileComposite(
        "import wing from \"wing.ips\"\nasset t { size 4x4 "
        "draw(v: i32, a: i32) { wing.draw(1, v, a, at = {0, 0}, mirror_x = 2) } }",
        "the mirror_x transform must be 0 or 1");
    compileComposite(
        "import wing from \"wing.ips\"\nasset t { size 4x4 "
        "draw(v: i32, a: i32) { wing.draw(1, v, a, at = {0, 0}, mirror_x = 1.5) } }",
        "must be an integer");
    for (const std::string name : {"rot", "mirror_x", "mirror_y"}) {
        compileComposite(
            "import wing from \"wing.ips\"\nasset t { size 4x4 "
            "draw(v: i32, a: i32) { wing.draw(1, v, a, at = {0, 0}, " +
                name + " = 0, " + name + " = 0) } }",
            "duplicate");
    }
    compileComposite(
        "import wing from \"wing.ips\"\nasset t { size 4x4 "
        "draw(v: i32, a: i32) { wing.draw(1, v, a, at = {0, 0}, rot = v) } }",
        "constant integers");
}

TEST_CASE("part helper writes inherit transforms and assembly preserves every D4 kind") {
    const ips::Program part = ips::parse(R"(
asset tile {
  size 4x2
  output point direct
  output point anchor
  output rect box
  output circle zone
  output list point anchors
  output list rect boxes
  output list circle zones
  draw(v: i32, a: i32) {
    pixel(0, 0, #ff0000)
    pixel(3, 1, #0000ff)
    direct = {1, 0.5}
    writeOutputs()
  }
  writeOutputs() {
    anchor = {1, 0.5}
    box = {0.5, 0.25, 2, 1}
    zone = {1, 0.5, 0.25}
    anchors = {1, 0.5}
    boxes = {0.5, 0.25, 2, 1}
    zones = {1, 0.5, 0.25}
  }
})");
    ips::checkAsset(part, {});
    // Independent expected coordinates for the two coloured source pixels,
    // the anchor, and the bounding rectangle of this non-square part.
    const i32 red[8][2] = {{0,0}, {0,3}, {3,1}, {1,0}, {3,0}, {0,1}, {0,0}, {1,3}};
    const i32 blue[8][2] = {{3,1}, {1,0}, {0,0}, {0,3}, {0,1}, {3,0}, {1,3}, {0,0}};
    const f32 points[8][2] = {{1,.5}, {.5,3}, {3,1.5}, {1.5,1}, {3,.5}, {1,1.5}, {.5,1}, {1.5,3}};
    const f32 rects[8][4] = {{.5,.25,2,1}, {.25,1.5,1,2}, {1.5,.75,2,1}, {.75,.5,1,2},
                            {1.5,.25,2,1}, {.5,.75,2,1}, {.25,.5,1,2}, {.75,1.5,1,2}};
    // All 16 combinations, with a literal composition table used only to
    // select the independent expected data above.
    const u8 kinds[4][2][2] = {{{0,5},{4,2}}, {{1,6},{7,3}}, {{2,4},{5,0}}, {{3,7},{6,1}}};
    for (i32 rot = 0; rot < 4; ++rot) {
        for (i32 mx = 0; mx < 2; ++mx) {
            for (i32 my = 0; my < 2; ++my) {
                const u8 kind = kinds[rot][mx][my];
                INFO("rot=" << rot * 90 << " mirror_x=" << mx << " mirror_y=" << my);
                const auto root = ips::parse(
                    "import tile from \"tile.ips\"\nasset t { size 8x8 output point p "
                    "draw(v: i32, a: i32) { tile.draw(1, v, a, at = {0,0}, rot = " +
                    std::to_string(rot * 90) + ", mirror_x = " + std::to_string(mx) +
                    ", mirror_y = " + std::to_string(my) + ") p = tile.outputs.anchor } }");
                ips::checkAsset(root, {{"tile", partInfoOf(part.asset)}});
                const auto bytes = compileProgram(root, {{"tile", &part}});
                const auto original = execute(bytes);
                const auto restored = execute(assemble(disassemble(bytes)));
                CHECK(restored.canvas.pixels == original.canvas.pixels);
                for (const auto* result : {&original, &restored}) {
                    CHECK(result->canvas.at(red[kind][0], red[kind][1]) == Color{255,0,0,255});
                    CHECK(result->canvas.at(blue[kind][0], blue[kind][1]) == Color{0,0,255,255});
                    for (const std::size_t slot : {std::size_t{0}, std::size_t{1}, std::size_t{2}, std::size_t{5}}) {
                        REQUIRE(result->outputs[slot].points.size() == 1);
                        CHECK(result->outputs[slot].points[0].x == points[kind][0]);
                        CHECK(result->outputs[slot].points[0].y == points[kind][1]);
                    }
                    for (const std::size_t slot : {std::size_t{3}, std::size_t{6}}) {
                        REQUIRE(result->outputs[slot].rects.size() == 1);
                        const auto& box = result->outputs[slot].rects[0];
                        CHECK(box.x == rects[kind][0]);
                        CHECK(box.y == rects[kind][1]);
                        CHECK(box.w == rects[kind][2]);
                        CHECK(box.h == rects[kind][3]);
                    }
                    for (const std::size_t slot : {std::size_t{4}, std::size_t{7}}) {
                        REQUIRE(result->outputs[slot].circles.size() == 1);
                        const auto& zone = result->outputs[slot].circles[0];
                        CHECK(zone.x == points[kind][0]);
                        CHECK(zone.y == points[kind][1]);
                        CHECK(zone.r == .25f);
                    }
                }
            }
        }
    }
}

TEST_CASE("transformed rect bounds use pinned NaN and signed-zero minNum") {
    const auto part = ips::parse(R"(
asset tile {
  size 4x2
  output rect zeros
  output rect nan_box
  draw(v: i32, a: i32) {
    zeros = {-0.0, -0.0, 0.0, 0.0}
    nan = -pow(-2, 0.5)
    nan_box = {nan, nan, 0, 0}
  }
})");
    const auto root = ips::parse(R"(
import tile from "tile.ips"
asset t {
  size 8x8
  draw(v: i32, a: i32) {
    tile.draw(1, v, a, at = {0,0}, rot = 90, mirror_y = true)
  }
})");
    ips::checkAsset(part, {});
    ips::checkAsset(root, {{"tile", partInfoOf(part.asset)}});
    const auto result = execute(compileProgram(root, {{"tile", &part}}));
    const auto& zeros = result.outputs[0].rects[0];
    CHECK(std::bit_cast<u32>(zeros.x) == 0x80000000u);
    CHECK(std::bit_cast<u32>(zeros.y) == 0x80000000u);
    CHECK(std::bit_cast<u32>(zeros.w) == 0u);
    CHECK(std::bit_cast<u32>(zeros.h) == 0u);
    const auto& nan = result.outputs[1].rects[0];
    CHECK(std::bit_cast<u32>(nan.x) == 0x7FC00000u);
    CHECK(std::bit_cast<u32>(nan.y) == 0x7FC00000u);
    CHECK(std::isnan(nan.w));
    CHECK(std::isnan(nan.h));
}

