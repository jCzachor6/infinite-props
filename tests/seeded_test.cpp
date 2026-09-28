#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <cstring>
#include <vector>

#include "support/test_support.hpp"

// Seeded primitives (spec §9.6/§9.7, exact algorithms §12). Golden vectors
// were computed from the spec formulas by an independent reference program
// and are pinned here bit-exactly.

using namespace test_support;

namespace {

u32 bits(f32 value) {
    u32 result = 0;
    std::memcpy(&result, &value, sizeof(result));
    return result;
}

f32 fromBits(u32 value) {
    f32 result = 0.f;
    std::memcpy(&result, &value, sizeof(result));
    return result;
}

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

} // namespace

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
        REQUIRE(bits(s1[i]) == seed1[i]);
    }

    const u32 seed0[] = {0x3bfe7280, 0x3c9f1a40, 0x3d78c1d0, 0x3ed74b42, 0x3caff6e0};
    const auto s0 = unitSequence(0, 5);
    for (unsigned i = 0; i < 5; ++i) {
        REQUIRE(bits(s0[i]) == seed0[i]);
    }
}

TEST_CASE("noise matches the pinned vectors (lattice points and midpoints)") {
    REQUIRE(bits(noiseAt(0.f, 0.f, 7)) == 0x3f148ba1u);
    REQUIRE(bits(noiseAt(1.f, 0.f, 7)) == 0x3f60728du);
    REQUIRE(bits(noiseAt(0.f, 1.f, 7)) == 0x3f6df2aau);
    REQUIRE(bits(noiseAt(0.5f, 0.5f, 7)) == 0x3f4153acu);
    REQUIRE(bits(noiseAt(3.7f, -2.2f, 7)) == 0x3f2fbbbcu);
    REQUIRE(bits(noiseAt(10.25f, 10.75f, 7)) == 0x3f40b9f7u);
    REQUIRE(bits(noiseAt(-0.5f, -0.25f, 3)) == 0x3ead603au); // negative coords
}

TEST_CASE("fault matches the pinned vectors and agrees with noise at corners") {
    REQUIRE(bits(faultAt(0.f, 0.f, 7)) == 0x3f148ba1u);
    REQUIRE(bits(faultAt(1.f, 0.f, 7)) == 0x3f60728du);
    REQUIRE(bits(faultAt(0.f, 1.f, 7)) == 0x3f6df2aau);
    REQUIRE(bits(faultAt(0.5f, 0.5f, 7)) == 0x3f67329cu);
    REQUIRE(bits(faultAt(3.7f, -2.2f, 7)) == 0x3f2f2202u);
    REQUIRE(bits(faultAt(10.25f, 10.75f, 7)) == 0x3f3b63f4u);
    REQUIRE(bits(faultAt(-0.5f, -0.25f, 3)) == 0x3e8ac167u);

    // At lattice points the triangular interpolation reduces to the corner
    // value, so fault and noise agree there.
    REQUIRE(bits(faultAt(0.f, 0.f, 7)) == bits(noiseAt(0.f, 0.f, 7)));
    REQUIRE(bits(faultAt(1.f, 0.f, 7)) == bits(noiseAt(1.f, 0.f, 7)));
}

TEST_CASE("smooth and lerp clamp their inputs") {
    REQUIRE(bits(evalProgF32(Prog{}.F(0.f).emit(0x72))) == 0x00000000u);
    REQUIRE(bits(evalProgF32(Prog{}.F(0.25f).emit(0x72))) == 0x3e200000u);
    REQUIRE(bits(evalProgF32(Prog{}.F(0.5f).emit(0x72))) == 0x3f000000u);
    REQUIRE(bits(evalProgF32(Prog{}.F(1.f).emit(0x72))) == 0x3f800000u);
    REQUIRE(bits(evalProgF32(Prog{}.F(2.f).emit(0x72))) == 0x3f800000u); // clamped
    REQUIRE(bits(evalProgF32(Prog{}.F(-1.f).emit(0x72))) == 0x00000000u); // clamped

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
    const f32 u = fromBits(0x3f1f5056u);
    const auto range = evalProgF32(Prog{}.I(1).emit(0x60).F(10.f).F(20.f).emit(0x63));
    REQUIRE(bits(range) == bits(10.f + (20.f - 10.f) * u));
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
    REQUIRE(bits(first) == bits(std::get<f32>(evalProg(c)[0])));
    REQUIRE(bits(second) == bits(std::get<f32>(evalProg(b)[0])));
}

TEST_CASE("stream.pop on an empty save stack traps") {
    expectProgTrap(Prog{}.emit(0x61), "stream stack underflow");
}

TEST_CASE("two VMs produce bit-identical sequences") {
    const Prog prog = Prog{}.F(3.7f).F(-2.2f).I(7).emit(0x70);
    const f32 a = evalProgF32(prog);
    const f32 b = evalProgF32(prog);
    REQUIRE(bits(a) == bits(b));
}
