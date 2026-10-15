#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "support/test_support.hpp"

#include "iprop/VM.hpp"

// Structured outputs (spec §5.4/§9.9): singular write-last, list append
// order, name resolution, global slot numbering.

using namespace iprop;
using namespace test_support;

namespace {

// Runs a single-draw program with the given declared outputs and returns
// the generation result.
GenerationResult runOutputs(const Prog& prog, const std::vector<OutputSpec>& outputs,
                            const std::vector<std::string>& names) {
    const auto meta = metaSection(8, 8, 0, 0, 100000, false, {}, 0, 0, 0, {}, outputs);
    const auto pool = constSection(names, prog.ints, prog.floats, prog.colors);
    const auto bytes = fullFile(meta, pool, codeSection({{0, {}, {}, {}, prog.body}}, {}));
    auto loaded = loadFile(bytes);
    VM vm(loaded, {});
    vm.run();
    return vm.result();
}

} // namespace

TEST_CASE("a singular output holds the last written value") {
    // slot 0: point "anchor"; written twice — the last value wins.
    Prog prog;
    prog.F(1.f).F(2.f).emit(0x90, 0); // out.point 0 = (1, 2)
    prog.F(3.f).F(4.f).emit(0x90, 0); // replace with (3, 4)

    const auto result = runOutputs(prog, {{0, 0, 1}}, {"draw", "anchor"});
    REQUIRE(result.outputs.size() == 1);
    REQUIRE(result.outputs[0].name == "anchor");
    REQUIRE(result.outputs[0].kind == OutputKind::Point);
    REQUIRE_FALSE(result.outputs[0].isList);
    REQUIRE(result.outputs[0].points.size() == 1);
    REQUIRE(result.outputs[0].points[0].x == 3.f);
    REQUIRE(result.outputs[0].points[0].y == 4.f);
}

TEST_CASE("list outputs preserve append order") {
    Prog prog;
    prog.F(1.f).F(1.f).emit(0x93, 0); // append (1, 1)
    prog.F(2.f).F(2.f).emit(0x93, 0); // append (2, 2)
    prog.F(3.f).F(3.f).emit(0x93, 0); // append (3, 3)

    const auto result = runOutputs(prog, {{0, 1, 1}}, {"draw", "anchors"});
    REQUIRE(result.outputs.size() == 1);
    REQUIRE(result.outputs[0].name == "anchors");
    REQUIRE(result.outputs[0].isList);
    REQUIRE(result.outputs[0].points.size() == 3);
    REQUIRE(result.outputs[0].points[0].x == 1.f);
    REQUIRE(result.outputs[0].points[1].x == 2.f);
    REQUIRE(result.outputs[0].points[2].x == 3.f);
    REQUIRE(result.outputs[0].points[2].y == 3.f);
}

TEST_CASE("rect and circle outputs carry their shapes") {
    Prog prog;
    prog.F(1.f).F(2.f).F(30.f).F(40.f).emit(0x91, 0); // out.rect 0 = (1,2,30,40)
    prog.F(5.f).F(6.f).F(7.f).emit(0x92, 1);          // out.circle 1 = (5,6,7)

    const auto result = runOutputs(prog, {{1, 0, 1}, {2, 0, 2}},
                                   {"draw", "collider", "hitbox"});
    REQUIRE(result.outputs.size() == 2);
    REQUIRE(result.outputs[0].name == "collider");
    REQUIRE(result.outputs[0].rects.size() == 1);
    REQUIRE(result.outputs[0].rects[0].w == 30.f);
    REQUIRE(result.outputs[0].rects[0].h == 40.f);
    REQUIRE(result.outputs[1].name == "hitbox");
    REQUIRE(result.outputs[1].circles.size() == 1);
    REQUIRE(result.outputs[1].circles[0].r == 7.f);
}

TEST_CASE("multiple outputs keep declaration order and find() works") {
    Prog prog;
    prog.F(0.f).F(0.f).emit(0x90, 0);
    prog.F(0.f).F(0.f).F(0.f).F(0.f).emit(0x94, 1);
    prog.F(0.f).F(0.f).F(0.f).emit(0x92, 2);

    const auto result = runOutputs(prog, {{0, 0, 1}, {1, 1, 2}, {2, 0, 3}},
                                   {"draw", "origin", "colliders", "zone"});
    REQUIRE(result.outputs.size() == 3);
    REQUIRE(result.outputs[0].name == "origin");
    REQUIRE(result.outputs[1].name == "colliders");
    REQUIRE(result.outputs[2].name == "zone");
    REQUIRE(result.find("origin") == &result.outputs[0]);
    REQUIRE(result.find("zone") == &result.outputs[2]);
    REQUIRE(result.find("missing") == nullptr);
}

TEST_CASE("no declared outputs produce an empty result") {
    Prog prog;
    prog.I(0).emit(0x16); // const + drop
    const auto result = runOutputs(prog, {}, {"draw"});
    REQUIRE(result.outputs.empty());
    REQUIRE(result.image.width == 8);
}

TEST_CASE("unvalidated wrong-slot writes trap") {
    // Slot 0 is declared as a rect; out.point targets it without validation.
    Prog prog;
    prog.F(0.f).F(0.f).emit(0x90, 0); // out.point 0
    const auto meta = metaSection(8, 8, 0, 0, 100000, false, {}, 0, 0, 0, {},
                                  {{1, 0, 1}}); // rect "collider"
    const auto pool = constSection({"draw", "collider"}, prog.ints, prog.floats, prog.colors);
    const auto bytes = fullFile(meta, pool, codeSection({{0, {}, {}, {}, prog.body}}, {}));
    auto loaded = loadFile(bytes);
    bool threw = false;
    try {
        VM vm(loaded, {});
        vm.run();
    } catch (const Trap& error) {
        threw = true;
        REQUIRE(std::string(error.what()).find("kind mismatch") != std::string::npos);
    }
    REQUIRE(threw);
}
