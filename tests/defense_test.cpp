#include <catch2/catch_test_macros.hpp>

#include "support/test_support.hpp"

// Runtime defenses on UNVALIDATED input: the VM must trap with a located
// message, never read out of bounds or misbehave (spec §8.4/§14).

using namespace test_support;

TEST_CASE("unvalidated type mismatches trap") {
    expectProgTrap(Prog{}.F(1.f).emit(0x20), "stack type mismatch"); // i32.add on f32
    expectProgTrap(Prog{}.I(1).emit(0x42), "stack type mismatch");   // f32.mul on i32
}

TEST_CASE("the reserved colour opcode traps") {
    expectProgTrap(Prog{}.emit(0x52), "reserved opcode");
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
}
