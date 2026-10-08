#include <catch2/catch_test_macros.hpp>

#include <limits>
#include <string>

#include "support/test_support.hpp"

#include "iprop/VM.hpp"

// Runtime defenses on UNVALIDATED input: the VM must trap with a located
// message, never read out of bounds or misbehave (spec §8.4/§14).

using namespace iprop;
using namespace test_support;

TEST_CASE("unvalidated type mismatches trap") {
    expectProgTrap(Prog{}.F(1.f).emit(0x20), "stack type mismatch"); // i32.add on f32
    expectProgTrap(Prog{}.I(1).emit(0x42), "stack type mismatch");   // f32.mul on i32
    for (const Opcode opcode : {Opcode::Vec2Mul, Opcode::Vec2Div}) {
        const auto op = static_cast<u8>(opcode);
        expectProgTrap(Prog{}.F(1.f).F(2.f).emit(0x5B).I(3).emit(op),
                       "stack type mismatch");
        expectProgTrap(Prog{}.F(1.f).F(2.f).emit(0x5B).C({1,2,3,4}).emit(op),
                       "stack type mismatch");
    }
}

TEST_CASE("the reserved colour opcode traps") {
    // 0x52 became color.set.a in v0.3 and 0xA4 color.r in v0.5; the
    // still-unassigned 0xFE traps.
    expectProgTrap(Prog{}.emit(0xFE), "reserved opcode");
    // The active color.set.a on an empty stack traps with the type
    // mismatch (defense in depth, never UB).
    expectProgTrap(Prog{}.emit(0x52), "stack");
}

TEST_CASE("out-of-range constant and local indices trap") {
    expectProgTrap(Prog{}.emit(0x10, 5), "constant index"); // empty i32 pool
    expectProgTrap(Prog{}.emit(0x11, 5), "constant index"); // empty f32 pool
    expectProgTrap(Prog{}.emit(0x12, 5), "constant index"); // empty colour pool
    expectProgTrap(Prog{}.emit(0x13, 9), "local index");    // only 2 params
    expectProgTrap(Prog{}.emit(0x14, 9), "local index");
}

TEST_CASE("out-of-range branches and dangling control flow trap") {
    expectProgTrap(Prog{}.emit(0x06, 5), "branch depth");       // br 5, no labels
    expectProgTrap(Prog{}.I(1).emit(0x07, 5), "branch depth"); // br_if 5, taken
    expectProgTrap(Prog{}.emit(0x04), "else without");          // else, no if
    expectProgTrap(Prog{}.emit(0x05), "end without");           // end, no block
    // An extra end after a balanced block: the second end traps instead of
    // crashing the control-flow precompute.
    expectProgTrap(Prog{}.emit(0x01).emit(0x05).emit(0x05), "end without");
}

TEST_CASE("non-finite or huge coordinates trap instead of UB") {
    const f32 nan = std::numeric_limits<f32>::quiet_NaN();
    // NaN in a poly vertex.
    expectProgTrap(Prog{}.F(nan).F(0.f).F(4.f).F(0.f).F(0.f).F(4.f)
                         .C(Color{1, 1, 1, 255}).emit(0x83, 3),
                   "coordinate out of range");
    // A huge oval (rx = 1e20f) whose bounding box would overflow i32.
    expectProgTrap(Prog{}.F(0.f).F(0.f).F(1.0e20f).F(1.f)
                         .C(Color{1, 1, 1, 255}).emit(0x84),
                   "coordinate out of range");
}

TEST_CASE("an oversized canvas traps at construction") {
    // meta sizeX above kMaxCanvasSide: the VM must refuse, not allocate.
    const auto meta = metaSection(20000, 8, 0, 0, 1, 1, 1000, false, {}, 0, 0, 0, {}, {});
    const auto pool = richPool({"draw"});
    const auto bytes = fullFile(meta, pool, codeSection({drawSpec(retIns())}, {}));
    auto loaded = loadFile(bytes);
    validate(loaded);
    bool threw = false;
    try {
        VM vm(loaded, {});
        (void)vm;
    } catch (const Trap& error) {
        threw = true;
        REQUIRE(std::string(error.what()).find("canvas too large") != std::string::npos);
    }
    REQUIRE(threw);
}
