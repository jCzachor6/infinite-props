#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <vector>

#include "support/test_support.hpp"

#include "iprop/VM.hpp"

using namespace iprop;
using namespace test_support;

namespace {

// Builds a single-draw file with the given body, fuel budget, locals and
// i32 constant pool (plus the "draw" name and one float/colour constant).
std::vector<u8> fileWith(const std::vector<u8>& body, u64 fuel,
                         const std::vector<u8>& locals = {},
                         const std::vector<i32>& ints = {0},
                         const std::vector<u8>& params = {0, 0}) {
    const auto meta = metaSection(8, 8, 0, 0, 1, 1, fuel, false, {}, 0, 0, 0, {}, {});
    const auto pool = constSection({"draw"}, ints, {0.f}, {{0, 0, 0, 255}});
    return fullFile(meta, pool, codeSection({{0, params, {}, locals, body}}, {}));
}

// Validates, runs, and returns the remaining fuel — the observable used to
// pin exact control-flow paths without output slots.
u64 runFuel(const std::vector<u8>& bytes) {
    auto loaded = loadFile(bytes);
    validate(loaded);
    VM vm(loaded, {});
    vm.run();
    return vm.fuel();
}

} // namespace

TEST_CASE("a loop with br_if sums 1..1000 in exactly 1000 iterations") {
    std::vector<u8> body;
    const auto ci = [&](u16 idx) { for (const auto b : ins(0x10, 0, idx)) body.push_back(b); };
    const auto op = [&](u8 code, u16 operand = 0) {
        for (const auto b : ins(code, 0, operand)) body.push_back(b);
    };
    ci(0); op(0x14, 2);              // sum = 0
    ci(1); op(0x14, 3);              // i = 1
    op(0x02);                        // loop
    op(0x13, 2); op(0x13, 3); op(0x20); op(0x14, 2); // sum += i
    op(0x13, 3); ci(1); op(0x20); op(0x14, 3);       // i += 1
    op(0x13, 3); ci(2); op(0x36); op(0x07, 0);       // br_if 0 when i <= 1000
    op(0x05);                        // end
    op(0x13, 2); op(0x16); op(0x09); // drop the sum, return

    const auto bytes = fileWith(body, 100000, {0, 0}, {0, 1, 1000});
    // 4 (init) + 1 (loop) + 1000 × 12 (body) + 1 (end) + 3 (tail) = 12009.
    REQUIRE(runFuel(bytes) == 100000 - 12009);
}

TEST_CASE("nested if/else with an early br skips the tail") {
    const auto make = [](u16 condIndex) {
        std::vector<u8> body;
        const auto op = [&](u8 code, u16 operand = 0) {
            for (const auto b : ins(code, 0, operand)) body.push_back(b);
        };
        op(0x01);             // block
        op(0x10, condIndex);  // const.i32 condition
        op(0x03);             // if
        op(0x10, 1); op(0x16); op(0x06, 1); // then: push, drop, br out of the block
        op(0x04);             // else
        op(0x10, 1); op(0x16);               // else: push, drop
        op(0x05);             // end if
        op(0x10, 1); op(0x16);               // tail (skipped by the early br)
        op(0x05);             // end block
        op(0x09);             // return
        return body;
    };
    // Taken: block, const, if, push, drop, br, end, return = 8.
    REQUIRE(runFuel(fileWith(make(1), 100, {}, {0, 1})) == 92);
    // Else: block, const, if, push, drop, end-if, push, drop, end, return = 10.
    REQUIRE(runFuel(fileWith(make(0), 100, {}, {0, 1})) == 90);
}

TEST_CASE("if without else runs both paths correctly") {
    const auto make = [](u16 condIndex) {
        std::vector<u8> body;
        const auto op = [&](u8 code, u16 operand = 0) {
            for (const auto b : ins(code, 0, operand)) body.push_back(b);
        };
        op(0x10, condIndex); // const.i32 condition
        op(0x03);            // if
        op(0x10, 1); op(0x16); // then: push, drop
        op(0x05);            // end
        op(0x09);            // return
        return body;
    };
    // Taken: const, if, push, drop, end, return = 6.
    REQUIRE(runFuel(fileWith(make(1), 100, {}, {0, 1})) == 94);
    // Not taken: const, if, end, return = 4.
    REQUIRE(runFuel(fileWith(make(0), 100, {}, {0, 1})) == 96);
}

TEST_CASE("an unconditional br inside a loop exhausts fuel at the loop start") {
    std::vector<u8> body;
    const auto op = [&](u8 code, u16 operand = 0) {
        for (const auto b : ins(code, 0, operand)) body.push_back(b);
    };
    op(0x02); // loop
    op(0x06, 0); // br 0 → back to the loop body, forever
    op(0x05); // end (unreachable)
    const auto bytes = fileWith(body, 100);

    auto loaded = loadFile(bytes);
    validate(loaded);
    bool threw = false;
    try {
        VM vm(loaded, {});
        vm.run();
    } catch (const Trap& error) {
        threw = true;
        REQUIRE(std::string(error.what()).find("fuel exhausted") != std::string::npos);
        // br 0 jumps to the loop body, which IS the br itself, so the
        // 101st instruction is the br at instruction index 1.
        const std::size_t base = loaded.layout.find(SectionId::Code)->offset;
        REQUIRE(error.offset == base + 28);
    }
    REQUIRE(threw);
}

TEST_CASE("br escapes ten nested blocks") {
    std::vector<u8> body;
    const auto op = [&](u8 code, u16 operand = 0) {
        for (const auto b : ins(code, 0, operand)) body.push_back(b);
    };
    for (int i = 0; i < 10; ++i) {
        op(0x01); // block
    }
    op(0x10, 1); // const.i32 1
    op(0x06, 9); // br 9 → the outermost end
    for (int i = 0; i < 10; ++i) {
        op(0x05); // end (only the outermost executes)
    }
    op(0x09); // return

    const auto bytes = fileWith(body, 100, {}, {0, 1});
    // 10 blocks + const + br + end + return = 14 instructions.
    REQUIRE(runFuel(bytes) == 86);
}
