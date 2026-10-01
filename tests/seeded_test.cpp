#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

#include "support/test_support.hpp"

#include "iprop/VM.hpp"

// Seeded primitives (spec §9.6/§9.7, exact algorithms §12). Golden vectors
// were computed from the spec formulas by an independent reference program
// and are pinned here bit-exactly.

using namespace iprop;
using namespace test_support;

namespace {

// stream.push(seed) followed by `count` rng.unit calls.
std::vector<f32> unitSequence(u32 seed, unsigned count) {
    Prog prog;
    prog.I(static_cast<i32>(seed)).emit(0x60); // stream.push
    for (unsigned i = 0; i < count; ++i) {
        prog.emit(0x62); // rng.unit
    }
    const auto stack = evalProg(prog);
    REQUIRE(stack.size() == count);
    std::vector<f32> result;
    for (const auto& value : stack) {
        REQUIRE(std::holds_alternative<f32>(value));
        result.push_back(std::get<f32>(value));
    }
    return result;
}

f32 noiseAt(f32 x, f32 y, u32 seed) {
    return evalProgF32(Prog{}.F(x).F(y).I(static_cast<i32>(seed)).emit(0x70));
}

f32 faultAt(f32 x, f32 y, u32 seed) {
    return evalProgF32(Prog{}.F(x).F(y).I(static_cast<i32>(seed)).emit(0x71));
}

f32 cellAt(f32 x, f32 y, u32 seed) {
    return evalProgF32(Prog{}.F(x).F(y).I(static_cast<i32>(seed)).emit(0x8B));
}

Vec2 jitterAt(i32 cx, i32 cy, u32 seed) {
    const auto stack = evalProg(Prog{}.I(cx).I(cy).I(static_cast<i32>(seed)).emit(0x8D));
    REQUIRE(stack.size() == 1);
    return std::get<Vec2>(stack[0]);
}

Prog noiseV2Program(u8 opcode, f32 x, f32 y) {
    Prog prog;
    prog.F(x).F(y);
    if (opcode == 0x8A || opcode == 0x8E) prog.I(8);
    if (opcode == 0x8C) prog.F(0.2f);
    return prog.I(7).emit(opcode);
}

// These are valid draw functions, so the failure must be a located runtime
// Trap rather than static rejection or undefined float-to-integer conversion.
void expectValidatedTrap(Prog prog, const char* message) {
    prog.emit(0x16); // drop the one result; draw returns no values
    auto loaded = loadFile(buildProgFile(prog));
    validate(loaded);
    bool threw = false;
    try {
        VM vm(loaded, {});
        vm.run();
    } catch (const Trap& error) {
        threw = true;
        REQUIRE(error.offset > 0);
        REQUIRE(std::string(error.what()).find(message) != std::string::npos);
    }
    REQUIRE(threw);
}

} // namespace

TEST_CASE("noise v2 rejects nonfinite and unindexable coordinates in validated files") {
    const f32 inf = std::numeric_limits<f32>::infinity();
    for (const u8 opcode : {0x89, 0x8A, 0x8B, 0x8C, 0x8E}) {
        for (const f32 coordinate : {inf, -inf, f32FromBits(0x7FC01234u),
                                     std::numeric_limits<f32>::max(),
                                     -std::numeric_limits<f32>::max(),
                                     2147483648.f, -2147483904.f}) {
            CAPTURE(opcode, coordinate);
            expectValidatedTrap(noiseV2Program(opcode, coordinate, 0.5f),
                                "noise coordinate out of range");
            expectValidatedTrap(noiseV2Program(opcode, 0.5f, coordinate),
                                "noise coordinate out of range");
        }
    }
}

TEST_CASE("noise v2 checks every octave and every warped sampling coordinate") {
    for (const u8 opcode : {0x8A, 0x8E}) {
        CAPTURE(opcode);
        // The original coordinate fits i32; octave 7's coordinate does not.
        expectValidatedTrap(Prog{}.F(20000000.f).F(0.5f).I(8).I(7).emit(opcode),
                            "noise coordinate out of range");
        REQUIRE(std::isfinite(evalProgF32(
            Prog{}.F(1000000.f).F(0.5f).I(8).I(7).emit(opcode))));
    }
    for (const f32 amount : {std::numeric_limits<f32>::infinity(),
                             -std::numeric_limits<f32>::infinity(),
                             f32FromBits(0x7FC01234u)}) {
        expectValidatedTrap(Prog{}.F(0.5f).F(0.5f).F(amount).I(7).emit(0x8C),
                            "warp amount out of range");
    }
    // A finite amount can move the outer fbm outside the lattice domain.
    expectValidatedTrap(Prog{}.F(0.5f).F(0.5f).F(3e30f).I(7).emit(0x8C),
                        "noise coordinate out of range");
    // q1 is safe at all four octaves, but q2's +9.1 offset rounds upward
    // and makes its last octave equal to 2^31, even with amount zero.
    expectValidatedTrap(Prog{}.F(0.5f).F(268435440.f).F(0.f).I(7).emit(0x8C),
                        "noise coordinate out of range");
}

TEST_CASE("gradient noise safely accepts the representable i32 lattice boundaries") {
    const f32 upper = std::nextafter(2147483648.f, 0.f);
    for (const f32 coordinate : {-2147483648.f, upper}) {
        REQUIRE(bitsOf(evalProgF32(Prog{}.F(coordinate).F(0.f).I(7).emit(0x89))) == 0);
        REQUIRE(bitsOf(evalProgF32(Prog{}.F(0.f).F(coordinate).I(7).emit(0x89))) == 0);
    }
}

TEST_CASE("Worley rejects cells outside its exactly representable neighbour domain") {
    for (const f32 coordinate : {-16777214.f, 16777214.f}) {
        REQUIRE(std::isfinite(cellAt(coordinate, 0.5f, 7)));
        REQUIRE(std::isfinite(cellAt(0.5f, coordinate, 7)));
    }
    for (const f32 coordinate : {-16777215.f, 16777215.f, -2147483648.f}) {
        expectValidatedTrap(Prog{}.F(coordinate).F(0.f).I(7).emit(0x8B),
                            "noise coordinate out of range");
        expectValidatedTrap(Prog{}.F(0.f).F(coordinate).I(7).emit(0x8B),
                            "noise coordinate out of range");
    }
}

TEST_CASE("cell jitter remains in its half-open cell after binary32 rounding") {
    for (const i32 cell : {std::numeric_limits<i32>::min(), -16777216, -1048576, -1572, -1, 0, 1571,
                           1048576, 16777216, 2147483520}) {
        for (const u32 seed : {0u, 7u, 2070u, 0xffffffffu}) {
            CAPTURE(cell, seed);
            const Vec2 point = jitterAt(cell, cell, seed);
            for (const f32 coordinate : {point.x, point.y}) {
                REQUIRE(static_cast<double>(coordinate) >= static_cast<double>(cell));
                REQUIRE(static_cast<double>(coordinate) < static_cast<double>(cell) + 1.0);
            }
        }
    }
    // Both used to round exactly to the next cell's lower boundary.
    REQUIRE(bitsOf(jitterAt(1571, 0, 7).x) == bitsOf(std::nextafter(1572.f, 1571.f)));
    REQUIRE(bitsOf(jitterAt(1048576, 0, 7).x) ==
            bitsOf(std::nextafter(1048577.f, 1048576.f)));
    for (const i32 cell : {16777217, -16777217, std::numeric_limits<i32>::max()}) {
        expectValidatedTrap(Prog{}.I(cell).I(0).I(7).emit(0x8D),
                            "cell index is not representable as f32");
        expectValidatedTrap(Prog{}.I(0).I(cell).I(7).emit(0x8D),
                            "cell index is not representable as f32");
    }
}

TEST_CASE("Worley finds the nearest feature two cells away without a lattice seam") {
    // At the origin seed 2070's nearest feature lies in (-2, 0), outside
    // the old 3x3 search. The expected bits come from that feature's distance.
    REQUIRE(bitsOf(cellAt(0.f, 0.f, 2070)) == 0x3F8F7256u);
    const Vec2 point = jitterAt(-2, 0, 2070);
    REQUIRE(bitsOf(cellAt(0.f, 0.f, 2070)) ==
            bitsOf(std::sqrt(point.x * point.x + point.y * point.y)));
    const f32 atOrigin = cellAt(0.f, 0.f, 2070);
    for (const f32 x : {-0.0001f, 0.0001f}) {
        REQUIRE(std::fabs(cellAt(x, 0.f, 2070) - atOrigin) <= 0.000101f);
    }
}

TEST_CASE("Worley F1 agrees with an exhaustive larger window of public jitter points") {
    const Vec2 queries[] = {{0, 0}, {-0.0001f, 0}, {0.5f, 0.5f},
                             {-3.25f, 4.75f}, {8.9999f, -7.0001f}, {1571.9f, 0.2f}};
    for (const Vec2 query : queries) {
        for (const u32 seed : {0u, 7u, 2070u, 0xffffffffu}) {
            CAPTURE(query.x, query.y, seed);
            const i32 ix = static_cast<i32>(std::floor(query.x));
            const i32 iy = static_cast<i32>(std::floor(query.y));
            f32 expected = std::numeric_limits<f32>::infinity();
            // Search 9x9 independently of the runtime's window size.
            for (i32 cy = iy - 4; cy <= iy + 4; ++cy) {
                for (i32 cx = ix - 4; cx <= ix + 4; ++cx) {
                    const Vec2 point = jitterAt(cx, cy, seed);
                    const f32 rx = point.x - query.x, ry = point.y - query.y;
                    expected = std::min(expected, std::sqrt(rx * rx + ry * ry));
                }
            }
            REQUIRE(bitsOf(cellAt(query.x, query.y, seed)) == bitsOf(expected));
        }
    }
}

TEST_CASE("hash matches the pinned splitmix32 vectors") {
    const auto hash = [](u32 x) {
        return static_cast<u32>(evalProgI32(Prog{}.I(static_cast<i32>(x)).emit(0x65)));
    };
    REQUIRE(hash(0) == 0x00000000u);
    REQUIRE(hash(1) == 0x688990c0u);
    REQUIRE(hash(2) == 0xd1132181u);
    REQUIRE(hash(0x9e3779b9u) == 0x01fce552u);
    REQUIRE(hash(0x7feb352du) == 0xd9fa482eu);
    REQUIRE(hash(0xFFFFFFFFu) == 0x6768824au);
    REQUIRE(hash(42) == 0x172733c2u);
    REQUIRE(hash(1234567) == 0xd79c6cceu);
}

TEST_CASE("rng.unit sequences match the pinned vectors") {
    const u32 seed1[] = {0x3f1f5056, 0x3e878a62, 0x3f72c606, 0x3eae0ff6, 0x3f6e493c,
                         0x3f671548, 0x3f2ec6f7, 0x3e1186c4, 0x3f4677ec, 0x3d8f06b0};
    const auto s1 = unitSequence(1, 10);
    for (unsigned i = 0; i < 10; ++i) {
        REQUIRE(bitsOf(s1[i]) == seed1[i]);
    }

    const u32 seed0[] = {0x3bfe7280, 0x3c9f1a40, 0x3d78c1d0, 0x3ed74b42, 0x3caff6e0};
    const auto s0 = unitSequence(0, 5);
    for (unsigned i = 0; i < 5; ++i) {
        REQUIRE(bitsOf(s0[i]) == seed0[i]);
    }
}

TEST_CASE("noise matches the pinned vectors (lattice points and midpoints)") {
    REQUIRE(bitsOf(noiseAt(0.f, 0.f, 7)) == 0x3f148ba1u);
    REQUIRE(bitsOf(noiseAt(1.f, 0.f, 7)) == 0x3f60728du);
    REQUIRE(bitsOf(noiseAt(0.f, 1.f, 7)) == 0x3f6df2aau);
    REQUIRE(bitsOf(noiseAt(0.5f, 0.5f, 7)) == 0x3f4153acu);
    REQUIRE(bitsOf(noiseAt(3.7f, -2.2f, 7)) == 0x3f2fbbbcu);
    REQUIRE(bitsOf(noiseAt(10.25f, 10.75f, 7)) == 0x3f40b9f7u);
    REQUIRE(bitsOf(noiseAt(-0.5f, -0.25f, 3)) == 0x3ead603au); // negative coords
}

TEST_CASE("fault matches the pinned vectors and agrees with noise at corners") {
    REQUIRE(bitsOf(faultAt(0.f, 0.f, 7)) == 0x3f148ba1u);
    REQUIRE(bitsOf(faultAt(1.f, 0.f, 7)) == 0x3f60728du);
    REQUIRE(bitsOf(faultAt(0.f, 1.f, 7)) == 0x3f6df2aau);
    REQUIRE(bitsOf(faultAt(0.5f, 0.5f, 7)) == 0x3f67329cu);
    REQUIRE(bitsOf(faultAt(3.7f, -2.2f, 7)) == 0x3f2f2202u);
    REQUIRE(bitsOf(faultAt(10.25f, 10.75f, 7)) == 0x3f3b63f4u);
    REQUIRE(bitsOf(faultAt(-0.5f, -0.25f, 3)) == 0x3e8ac167u);

    // At lattice points the triangular interpolation reduces to the corner
    // value, so fault and noise agree there.
    REQUIRE(bitsOf(faultAt(0.f, 0.f, 7)) == bitsOf(noiseAt(0.f, 0.f, 7)));
    REQUIRE(bitsOf(faultAt(1.f, 0.f, 7)) == bitsOf(noiseAt(1.f, 0.f, 7)));
}

TEST_CASE("smooth and lerp clamp their inputs") {
    REQUIRE(bitsOf(evalProgF32(Prog{}.F(0.f).emit(0x72))) == 0x00000000u);
    REQUIRE(bitsOf(evalProgF32(Prog{}.F(0.25f).emit(0x72))) == 0x3e200000u);
    REQUIRE(bitsOf(evalProgF32(Prog{}.F(0.5f).emit(0x72))) == 0x3f000000u);
    REQUIRE(bitsOf(evalProgF32(Prog{}.F(1.f).emit(0x72))) == 0x3f800000u);
    REQUIRE(bitsOf(evalProgF32(Prog{}.F(2.f).emit(0x72))) == 0x3f800000u); // clamped
    REQUIRE(bitsOf(evalProgF32(Prog{}.F(-1.f).emit(0x72))) == 0x00000000u); // clamped

    const auto lerp = [](f32 t) { return evalProgF32(Prog{}.F(1.f).F(3.f).F(t).emit(0x73)); };
    REQUIRE(lerp(0.f) == 1.f);
    REQUIRE(lerp(0.5f) == 2.f);
    REQUIRE(lerp(1.f) == 3.f);
    REQUIRE(lerp(2.f) == 3.f);  // clamped
    REQUIRE(lerp(-1.f) == 1.f); // clamped
}

TEST_CASE("rng.index and rng.range follow the pinned stream") {
    // index(seed 1): golden values for n = 0, 1, 5, 100.
    const auto index = [](u32 n) {
        return evalProgI32(Prog{}.I(1).emit(0x60).I(static_cast<i32>(n)).emit(0x64));
    };
    REQUIRE(index(0) == 0);   // n == 0 -> 0
    REQUIRE(index(1) == 0);
    REQUIRE(index(5) == 3);
    REQUIRE(index(100) == 62);

    // range(10, 20) with seed 1 uses the first unit() of the stream.
    const f32 u = f32FromBits(0x3f1f5056u);
    const auto range = evalProgF32(Prog{}.I(1).emit(0x60).F(10.f).F(20.f).emit(0x63));
    REQUIRE(bitsOf(range) == bitsOf(10.f + (20.f - 10.f) * u));
}

TEST_CASE("stream.push/pop restores the previous stream exactly") {
    // A: push 5, push 9, unit (from 9), pop, unit (from 5).
    Prog a;
    a.I(5).emit(0x60).I(9).emit(0x60).emit(0x62).emit(0x61).emit(0x62);
    // B: push 5, unit.
    Prog b;
    b.I(5).emit(0x60).emit(0x62);
    // C: push 9, unit.
    Prog c;
    c.I(9).emit(0x60).emit(0x62);

    const auto stackA = evalProg(a);
    REQUIRE(stackA.size() == 2);
    const f32 first = std::get<f32>(stackA[0]);
    const f32 second = std::get<f32>(stackA[1]);
    REQUIRE(bitsOf(first) == bitsOf(std::get<f32>(evalProg(c)[0])));
    REQUIRE(bitsOf(second) == bitsOf(std::get<f32>(evalProg(b)[0])));
}

TEST_CASE("stream.state exposes the raw stream value") {
    // stream.state before any draw equals the seed; after a unit it changes.
    REQUIRE(evalProgI32(Prog{}.emit(0x66)) == 0); // seed 0
    REQUIRE(evalProgI32(Prog{}.I(7).emit(0x60).emit(0x66)) == 7);
    // The derived-stream pattern: Random{hash(seed ^ const)}.
    REQUIRE(static_cast<u32>(evalProgI32(Prog{}.I(7).emit(0x60).emit(0x66)
                                             .I(0xc4a7ac7e).emit(0x29).emit(0x65))) ==
            static_cast<u32>(evalProgI32(Prog{}.I(0xc4a7ac7e ^ 7).emit(0x65))));
}

TEST_CASE("stream.pop on an empty save stack traps") {
    expectProgTrap(Prog{}.emit(0x61), "stream stack underflow");
}

TEST_CASE("two VMs produce bit-identical sequences") {
    const Prog prog = Prog{}.F(3.7f).F(-2.2f).I(7).emit(0x70);
    const f32 a = evalProgF32(prog);
    const f32 b = evalProgF32(prog);
    REQUIRE(bitsOf(a) == bitsOf(b));
}

TEST_CASE("call frames share the caller's stream like a reference (spec §8.2)") {
    // Draw: push stream 1, call a helper that consumes u0, drop, then two
    // units — they must be u1 and u2, proving the helper's advances
    // propagated back to the caller.
    std::vector<u8> drawBody;
    for (const auto b : ins(0x10, 0, 0)) drawBody.push_back(b); // const.i32 1
    for (const auto b : ins(0x60, 0, 0)) drawBody.push_back(b); // stream.push
    for (const auto b : ins(0x08, 0, 1)) drawBody.push_back(b); // call 1
    for (const auto b : ins(0x16, 0, 0)) drawBody.push_back(b); // drop (u0)
    for (const auto b : ins(0x62, 0, 0)) drawBody.push_back(b); // unit → u1
    for (const auto b : ins(0x62, 0, 0)) drawBody.push_back(b); // unit → u2

    std::vector<u8> helperBody;
    for (const auto b : ins(0x62, 0, 0)) helperBody.push_back(b); // unit
    for (const auto b : ins(0x09, 0, 0)) helperBody.push_back(b); // return

    const auto funcs = std::vector<FuncSpec>{
        {0, {0, 0}, {}, {}, drawBody},
        {kAnonymousName, {}, {1}, {}, helperBody}, // result: f32
    };
    const auto meta = metaSection(8, 8, 0, 0, 1, 1, 100000, false, {}, 0, 0, 0, {}, {});
    const auto pool = constSection({"draw"}, {1}, {0.f}, {{0, 0, 0, 255}});
    const auto bytes = fullFile(meta, pool, codeSection(funcs, {}));

    auto loaded = loadFile(bytes);
    VM vm(loaded, {});
    vm.run();
    const auto& stack = vm.debugStack();
    REQUIRE(stack.size() == 2);
    REQUIRE(std::holds_alternative<f32>(stack[0]));
    REQUIRE(std::holds_alternative<f32>(stack[1]));

    const auto reference = unitSequence(1, 3); // u0, u1, u2
    REQUIRE(bitsOf(std::get<f32>(stack[0])) == bitsOf(reference[1]));
    REQUIRE(bitsOf(std::get<f32>(stack[1])) == bitsOf(reference[2]));
}
