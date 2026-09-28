#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "support/test_support.hpp"

#include "iprop/VM.hpp"

// Parts and composition (spec §9.10/§10): sub-canvas rendering, pinned blit
// mapping, part-output register, nesting, stream isolation.

using namespace iprop;
using namespace test_support;

namespace {

// Appends a full part.draw call to a body: seed, variant, archetype, atX,
// atY (spec §9.10 stack order), using const-pool indices from the program.
void pushPartCall(std::vector<u8>& body, u16 partIndex, u16 seedIdx = 0, u16 variantIdx = 0,
                  u16 archetypeIdx = 0, u16 atXIdx = 0, u16 atYIdx = 0) {
    for (const auto b : ins(0x10, 0, seedIdx)) body.push_back(b);
    for (const auto b : ins(0x10, 0, variantIdx)) body.push_back(b);
    for (const auto b : ins(0x10, 0, archetypeIdx)) body.push_back(b);
    for (const auto b : ins(0x11, 0, atXIdx)) body.push_back(b);
    for (const auto b : ins(0x11, 0, atYIdx)) body.push_back(b);
    for (const auto b : ins(0xA0, 0, partIndex)) body.push_back(b);
}

// Builds and runs a file with the given draw body, part functions and part
// metas; returns the result.
GenerationResult runParts(const std::vector<u8>& drawBody,
                          const std::vector<FuncSpec>& partFunctions,
                          const std::vector<PartSpec>& parts,
                          const std::vector<OutputSpec>& rootOutputs,
                          const std::vector<std::string>& names,
                          const std::vector<i32>& ints, const std::vector<f32>& floats,
                          const std::vector<Color>& colors, u32 sizeX = 8, u32 sizeY = 8) {
    std::vector<FuncSpec> funcs;
    funcs.push_back({0, {0, 0}, {}, {}, drawBody});
    funcs.insert(funcs.end(), partFunctions.begin(), partFunctions.end());
    const auto meta = metaSection(sizeX, sizeY, 0, 0, 1, 1, 1000000, false, {}, 0, 0, 0, {},
                                  rootOutputs);
    const auto pool = constSection(names, ints, floats, colors);
    const auto bytes = fullFile(meta, pool, codeSection(funcs, parts));
    auto loaded = loadFile(bytes);
    VM vm(loaded, {});
    vm.run();
    return vm.result();
}

} // namespace

TEST_CASE("a part draws onto its sub-canvas and blits to the pinned origin") {
    // Part: pixel(2, 3), 8x8 sub-canvas, origin 0,0; called at (0,0).
    std::vector<u8> partBody;
    for (const auto b : ins(0x10, 0, 0)) partBody.push_back(b); // const.i32 2 (idx 0)
    for (const auto b : ins(0x10, 0, 1)) partBody.push_back(b); // const.i32 3 (idx 1)
    for (const auto b : ins(0x12, 0, 0)) partBody.push_back(b); // const.color 0
    for (const auto b : ins(0x80, 0, 0)) partBody.push_back(b); // pixel
    for (const auto b : retIns()) partBody.push_back(b);

    std::vector<u8> drawBody;
    pushPartCall(drawBody, 0);
    for (const auto b : retIns()) drawBody.push_back(b);

    const Color c{9, 8, 7, 255};
    const auto result = runParts(drawBody, {{kAnonymousName, {0, 0}, {}, {}, partBody}},
                                 {{1, 8, 8, 0, 0, {}, {}}}, {}, {"draw"},
                                 {2, 3, 0}, {0.f}, {c});
    REQUIRE(result.image.at(2, 3) == c);
    REQUIRE(result.image.at(2, 2) == Color{0, 0, 0, 0});
}

TEST_CASE("the blit respects the part origin") {
    // Part origin (2,1): its pixel(0,0) lands at atX-2, atY-1.
    std::vector<u8> partBody;
    for (const auto b : ins(0x10, 0, 0)) partBody.push_back(b); // const.i32 0 (x)
    for (const auto b : ins(0x10, 0, 0)) partBody.push_back(b); // const.i32 0 (y)
    for (const auto b : ins(0x12, 0, 0)) partBody.push_back(b); // const.color
    for (const auto b : ins(0x80, 0, 0)) partBody.push_back(b); // pixel
    for (const auto b : retIns()) partBody.push_back(b);

    std::vector<u8> drawBody;
    // at (10, 10): the part's (0,0) lands at (8, 9). Pool floats need 10.0.
    pushPartCall(drawBody, 0, 0, 0, 0, 0, 0);
    for (const auto b : retIns()) drawBody.push_back(b);

    const Color c{1, 2, 3, 255};
    const auto result = runParts(drawBody, {{kAnonymousName, {0, 0}, {}, {}, partBody}},
                                 {{1, 16, 16, 2.f, 1.f, {}, {}}}, {}, {"draw"},
                                 {0}, {10.f}, {c}, 16, 16);
    REQUIRE(result.image.at(8, 9) == c);
    REQUIRE(result.image.at(10, 10) == Color{0, 0, 0, 0});
}

TEST_CASE("parts clip to their own sub-canvas") {
    // A 4x4 part drawing at (10, 0) paints nothing; (1,1) paints.
    std::vector<u8> partBody;
    for (const auto b : ins(0x10, 0, 1)) partBody.push_back(b); // const.i32 10 (x)
    for (const auto b : ins(0x10, 0, 0)) partBody.push_back(b); // const.i32 0 (y)
    for (const auto b : ins(0x12, 0, 0)) partBody.push_back(b);
    for (const auto b : ins(0x80, 0, 0)) partBody.push_back(b); // pixel(10, 0) → clipped
    for (const auto b : ins(0x10, 0, 0)) partBody.push_back(b); // const.i32 0 (x)
    for (const auto b : ins(0x10, 0, 0)) partBody.push_back(b); // const.i32 0 (y)
    for (const auto b : ins(0x12, 0, 0)) partBody.push_back(b);
    for (const auto b : ins(0x80, 0, 0)) partBody.push_back(b); // pixel(0, 0)
    for (const auto b : ins(0x10, 0, 2)) partBody.push_back(b); // const.i32 1? idx 2
    for (const auto b : ins(0x10, 0, 2)) partBody.push_back(b);
    for (const auto b : ins(0x12, 0, 0)) partBody.push_back(b);
    for (const auto b : ins(0x80, 0, 0)) partBody.push_back(b); // pixel(1, 1)
    for (const auto b : retIns()) partBody.push_back(b);

    std::vector<u8> drawBody;
    pushPartCall(drawBody, 0);
    for (const auto b : retIns()) drawBody.push_back(b);

    const Color c{4, 5, 6, 255};
    const auto result = runParts(drawBody, {{kAnonymousName, {0, 0}, {}, {}, partBody}},
                                 {{1, 4, 4, 0, 0, {}, {}}}, {}, {"draw"},
                                 {0, 10, 1}, {0.f}, {c});
    REQUIRE(result.image.at(0, 0) == c);
    REQUIRE(result.image.at(1, 1) == c);
    REQUIRE(result.image.at(10, 0) == Color{0, 0, 0, 0}); // clipped
}

TEST_CASE("a parent reads the part's outputs and propagates them (the portrait case)") {
    // Part: out.point(32, 24) into its global slot 1.
    std::vector<u8> partBody;
    for (const auto b : ins(0x11, 0, 0)) partBody.push_back(b); // const.f32 32
    for (const auto b : ins(0x11, 0, 1)) partBody.push_back(b); // const.f32 24
    for (const auto b : ins(0x90, 0, 1)) partBody.push_back(b); // out.point 1
    for (const auto b : retIns()) partBody.push_back(b);

    // Draw: part.draw, part.get.point 1 → out.point 0 (root slot).
    std::vector<u8> drawBody;
    pushPartCall(drawBody, 0);
    for (const auto b : ins(0x96, 0, 1)) drawBody.push_back(b); // part.get.point 1
    for (const auto b : ins(0x90, 0, 0)) drawBody.push_back(b); // out.point 0
    for (const auto b : retIns()) drawBody.push_back(b);

    const auto result = runParts(drawBody, {{kAnonymousName, {0, 0}, {}, {}, partBody}},
                                 {{1, 8, 8, 0, 0, {}, {{0, 0, 2}}}},
                                 {{0, 0, 1}},                     // root: point "pupilCenter"
                                 {"draw", "pupilCenter", "pupil"},
                                 {0}, {32.f, 24.f}, {});
    REQUIRE(result.outputs.size() == 2);
    REQUIRE(result.outputs[0].name == "pupilCenter");
    REQUIRE(result.outputs[0].points.size() == 1);
    REQUIRE(result.outputs[0].points[0].x == 32.f);
    REQUIRE(result.outputs[0].points[0].y == 24.f);
    REQUIRE(result.outputs[1].name == "pupil");
    REQUIRE(result.outputs[1].points[0].x == 32.f);
}

TEST_CASE("nested parts compose offsets") {
    // Part A: pixel(1, 1).
    std::vector<u8> bodyA;
    for (const auto b : ins(0x10, 0, 1)) bodyA.push_back(b); // x = 1
    for (const auto b : ins(0x10, 0, 1)) bodyA.push_back(b); // y = 1
    for (const auto b : ins(0x12, 0, 0)) bodyA.push_back(b);
    for (const auto b : ins(0x80, 0, 0)) bodyA.push_back(b);
    for (const auto b : retIns()) bodyA.push_back(b);

    // Part B: pixel(0, 0), then part.draw A at (2, 2).
    std::vector<u8> bodyB;
    for (const auto b : ins(0x10, 0, 0)) bodyB.push_back(b); // x = 0
    for (const auto b : ins(0x10, 0, 0)) bodyB.push_back(b); // y = 0
    for (const auto b : ins(0x12, 0, 0)) bodyB.push_back(b);
    for (const auto b : ins(0x80, 0, 0)) bodyB.push_back(b);
    pushPartCall(bodyB, 0, 0, 0, 0, 1, 1); // A at (2, 2): floats idx 1 = 2.0
    for (const auto b : retIns()) bodyB.push_back(b);

    std::vector<u8> drawBody;
    pushPartCall(drawBody, 1, 0, 0, 0, 1, 1); // B at (2, 2)
    for (const auto b : retIns()) drawBody.push_back(b);

    const Color c{2, 3, 4, 255};
    const auto result = runParts(
        drawBody,
        {{kAnonymousName, {0, 0}, {}, {}, bodyA}, {kAnonymousName, {0, 0}, {}, {}, bodyB}},
        {{1, 8, 8, 0, 0, {}, {}}, {2, 8, 8, 0, 0, {}, {}}}, {}, {"draw"},
        {0, 1}, {0.f, 2.f}, {c});
    // B's (0,0) lands at (2,2); A at (2,2) inside B shifts its raster by
    // another (2,2), so A's pixel(1,1) lands at main (5,5).
    REQUIRE(result.image.at(2, 2) == c);
    REQUIRE(result.image.at(5, 5) == c);
}

TEST_CASE("call order is paint order") {
    // Part A paints (5,5) red; part B paints (5,5) blue; B called last wins.
    std::vector<u8> bodyA;
    for (const auto b : ins(0x10, 0, 1)) bodyA.push_back(b); // x = 5
    for (const auto b : ins(0x10, 0, 1)) bodyA.push_back(b); // y = 5
    for (const auto b : ins(0x12, 0, 0)) bodyA.push_back(b); // red
    for (const auto b : ins(0x80, 0, 0)) bodyA.push_back(b);
    for (const auto b : retIns()) bodyA.push_back(b);

    std::vector<u8> bodyB;
    for (const auto b : ins(0x10, 0, 1)) bodyB.push_back(b);
    for (const auto b : ins(0x10, 0, 1)) bodyB.push_back(b);
    for (const auto b : ins(0x12, 0, 1)) bodyB.push_back(b); // blue
    for (const auto b : ins(0x80, 0, 0)) bodyB.push_back(b);
    for (const auto b : retIns()) bodyB.push_back(b);

    std::vector<u8> drawBody;
    pushPartCall(drawBody, 0); // A
    pushPartCall(drawBody, 1); // B
    for (const auto b : retIns()) drawBody.push_back(b);

    const Color red{255, 0, 0, 255};
    const Color blue{0, 0, 255, 255};
    const auto result = runParts(
        drawBody,
        {{kAnonymousName, {0, 0}, {}, {}, bodyA}, {kAnonymousName, {0, 0}, {}, {}, bodyB}},
        {{1, 8, 8, 0, 0, {}, {}}, {2, 8, 8, 0, 0, {}, {}}}, {}, {"draw"},
        {0, 5}, {0.f}, {red, blue});
    REQUIRE(result.image.at(5, 5) == blue);
}

TEST_CASE("parts get their own stream; the caller's stream is untouched") {
    // Part: two units, out.point 2 (its own global slot).
    std::vector<u8> partBody;
    for (const auto b : ins(0x62, 0, 0)) partBody.push_back(b); // unit
    for (const auto b : ins(0x62, 0, 0)) partBody.push_back(b); // unit
    for (const auto b : ins(0x90, 0, 2)) partBody.push_back(b); // out.point 2
    for (const auto b : retIns()) partBody.push_back(b);

    // Draw: push stream 5, part.draw (seed 1), part.get → root out.point 0,
    // then unit from the caller's stream + const.f32 0 → root out.point 2.
    std::vector<u8> drawBody;
    for (const auto b : ins(0x10, 0, 2)) drawBody.push_back(b); // const.i32 5 (idx 2)
    for (const auto b : ins(0x60, 0, 0)) drawBody.push_back(b); // stream.push
    pushPartCall(drawBody, 0, 1, 0, 0, 0, 0); // seed = 1 (int idx 1)
    for (const auto b : ins(0x96, 0, 2)) drawBody.push_back(b); // part.get.point 2
    for (const auto b : ins(0x90, 0, 0)) drawBody.push_back(b); // out.point 0 (propagated)
    for (const auto b : ins(0x62, 0, 0)) drawBody.push_back(b); // unit (caller stream 5)
    for (const auto b : ins(0x11, 0, 0)) drawBody.push_back(b); // const.f32 0
    for (const auto b : ins(0x90, 0, 1)) drawBody.push_back(b); // out.point 1 ("after")
    for (const auto b : retIns()) drawBody.push_back(b);

    const auto result = runParts(
        drawBody, {{kAnonymousName, {0, 0}, {}, {}, partBody}},
        {{1, 8, 8, 0, 0, {}, {{0, 0, 2}}}},
        {{0, 0, 1}, {0, 0, 3}}, // root: "pupil", "after"
        {"draw", "pupil", "partPupil", "after"},
        {0, 1, 5}, {0.f}, {});

    // Global slots: 0 = root "pupil" (propagated), 1 = root "after",
    // 2 = part "partPupil". The part's units come from seed 1: pinned
    // goldens.
    REQUIRE(result.outputs[0].points[0].x == result.outputs[2].points[0].x);
    REQUIRE(bitsOf(result.outputs[0].points[0].x) == 0x3f1f5056u);
    REQUIRE(bitsOf(result.outputs[0].points[0].y) == 0x3e878a62u);

    // The caller's stream (seed 5) was untouched by the part: its first unit
    // equals the reference computed independently.
    const f32 reference = evalProgF32(Prog{}.I(5).emit(0x60).emit(0x62));
    REQUIRE(bitsOf(result.outputs[1].points[0].x) == bitsOf(reference));
}

TEST_CASE("part.get without a part call traps") {
    // part.get.point 0 (the part's slot) before any part.draw: the register
    // is empty.
    std::vector<u8> drawBody;
    for (const auto b : ins(0x96, 0, 0)) drawBody.push_back(b);
    for (const auto b : retIns()) drawBody.push_back(b);

    const auto meta = metaSection(8, 8, 0, 0, 1, 1, 1000000, false, {}, 0, 0, 0, {}, {});
    const auto pool = constSection({"draw", "pupil"}, {0}, {0.f}, {{0, 0, 0, 255}});
    const auto bytes = fullFile(meta, pool,
                                codeSection({{0, {0, 0}, {}, {}, drawBody},
                                             {kAnonymousName, {0, 0}, {}, {}, retIns()}},
                                            {{1, 8, 8, 0, 0, {}, {{0, 0, 1}}}}));
    auto loaded = loadFile(bytes);
    bool threw = false;
    try {
        VM vm(loaded, {});
        vm.run();
    } catch (const Trap& error) {
        threw = true;
        REQUIRE(std::string(error.what()).find("last called part") != std::string::npos);
    }
    REQUIRE(threw);
}
