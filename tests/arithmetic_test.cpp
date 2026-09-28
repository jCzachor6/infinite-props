#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstring>
#include <limits>
#include <string>

#include <vector>

#include "support/test_support.hpp"

#include "iprop/VM.hpp"

using namespace iprop;
using namespace test_support;

namespace {

// A tiny program builder: deduplicates constants into the const pool and
// emits instructions. Programs leave one value on the entry stack and are
// executed WITHOUT validation (draw would require zero results otherwise);
// the VM's runtime type checks still apply.
struct Prog {
    std::vector<u8> body;
    std::vector<i32> ints;
    std::vector<f32> floats;
    std::vector<Color> colors;

    Prog& emit(u8 opcode, u16 operand = 0) {
        for (const auto b : ins(opcode, 0, operand)) body.push_back(b);
        return *this;
    }

    Prog& I(i32 value) {
        u16 index = 0;
        for (u16 i = 0; i < ints.size(); ++i) {
            if (ints[i] == value) {
                index = i;
                goto foundInt;
            }
        }
        ints.push_back(value);
        index = static_cast<u16>(ints.size() - 1);
    foundInt:
        emit(0x10, index);
        return *this;
    }

    Prog& F(f32 value) {
        u16 index = 0;
        for (u16 i = 0; i < floats.size(); ++i) {
            if (std::memcmp(&floats[i], &value, sizeof(f32)) == 0) {
                index = i;
                goto foundFloat;
            }
        }
        floats.push_back(value);
        index = static_cast<u16>(floats.size() - 1);
    foundFloat:
        emit(0x11, index);
        return *this;
    }

    Prog& C(Color value) {
        u16 index = 0;
        for (u16 i = 0; i < colors.size(); ++i) {
            if (colors[i] == value) {
                index = i;
                goto foundColor;
            }
        }
        colors.push_back(value);
        index = static_cast<u16>(colors.size() - 1);
    foundColor:
        emit(0x12, index);
        return *this;
    }
};

std::vector<u8> buildFile(const Prog& prog) {
    const auto meta = metaSection(8, 8, 0, 0, 1, 1, 100000, false, {}, 0, 0, 0, {}, {});
    const auto pool = constSection({"draw"}, prog.ints, prog.floats, prog.colors);
    return fullFile(meta, pool, codeSection({{0, {0, 0}, {}, {}, prog.body}}, {}));
}

// Runs without validation and returns the entry frame's final stack.
std::vector<Value> eval(const Prog& prog) {
    // The file bytes must outlive the LoadedFile: its spans point into them.
    const auto bytes = buildFile(prog);
    auto loaded = loadFile(bytes);
    VM vm(loaded, {});
    vm.run();
    return vm.debugStack();
}

i32 evalI32(const Prog& prog) {
    const auto stack = eval(prog);
    REQUIRE(stack.size() == 1);
    REQUIRE(std::holds_alternative<i32>(stack[0]));
    return std::get<i32>(stack[0]);
}

f32 evalF32(const Prog& prog) {
    const auto stack = eval(prog);
    REQUIRE(stack.size() == 1);
    REQUIRE(std::holds_alternative<f32>(stack[0]));
    return std::get<f32>(stack[0]);
}

Color evalColor(const Prog& prog) {
    const auto stack = eval(prog);
    REQUIRE(stack.size() == 1);
    REQUIRE(std::holds_alternative<Color>(stack[0]));
    return std::get<Color>(stack[0]);
}

void expectArithmeticTrap(const Prog& prog, const char* messagePart) {
    const auto bytes = buildFile(prog); // must outlive the LoadedFile spans
    auto loaded = loadFile(bytes);
    bool threw = false;
    try {
        VM vm(loaded, {});
        vm.run();
    } catch (const Trap& error) {
        threw = true;
        REQUIRE(std::string(error.what()).find(messagePart) != std::string::npos);
    }
    REQUIRE(threw);
}

u32 bits(f32 value) {
    u32 result = 0;
    std::memcpy(&result, &value, sizeof(result));
    return result;
}

constexpr i32 kMin = std::numeric_limits<i32>::min();
constexpr i32 kMax = std::numeric_limits<i32>::max();

} // namespace

TEST_CASE("i32 add/sub/mul wrap in two's complement") {
    REQUIRE(evalI32(Prog{}.I(1).I(2).emit(0x20)) == 3);
    REQUIRE(evalI32(Prog{}.I(kMax).I(1).emit(0x20)) == kMin);   // wraps
    REQUIRE(evalI32(Prog{}.I(5).I(8).emit(0x21)) == -3);
    REQUIRE(evalI32(Prog{}.I(kMin).I(1).emit(0x21)) == kMax);   // wraps
    REQUIRE(evalI32(Prog{}.I(3).I(4).emit(0x22)) == 12);
    REQUIRE(evalI32(Prog{}.I(0x40000000).I(4).emit(0x22)) == 0); // 2^30 * 4 wraps
    REQUIRE(evalI32(Prog{}.I(-2).I(3).emit(0x22)) == -6);
}

TEST_CASE("i32 division and remainder follow C truncation semantics") {
    REQUIRE(evalI32(Prog{}.I(10).I(3).emit(0x23)) == 3);
    REQUIRE(evalI32(Prog{}.I(-10).I(3).emit(0x23)) == -3);
    REQUIRE(evalI32(Prog{}.I(10).I(-3).emit(0x23)) == -3);
    REQUIRE(evalI32(Prog{}.I(-10).I(-3).emit(0x23)) == 3);

    REQUIRE(evalI32(Prog{}.I(10).I(3).emit(0x25)) == 1);
    REQUIRE(evalI32(Prog{}.I(-10).I(3).emit(0x25)) == -1);
    REQUIRE(evalI32(Prog{}.I(10).I(-3).emit(0x25)) == 1);
    REQUIRE(evalI32(Prog{}.I(-10).I(-3).emit(0x25)) == -1);
    // INT32_MIN % -1 is 0, not UB.
    REQUIRE(evalI32(Prog{}.I(kMin).I(-1).emit(0x25)) == 0);

    // Unsigned: 0xFFFFFFFF / 3 = 0x55555555, remainder 0.
    REQUIRE(evalI32(Prog{}.I(-1).I(3).emit(0x24)) == static_cast<i32>(0x55555555u));
    REQUIRE(evalI32(Prog{}.I(-1).I(3).emit(0x26)) == 0);
}

TEST_CASE("division and remainder by zero trap") {
    expectArithmeticTrap(Prog{}.I(1).I(0).emit(0x23), "divide by zero");
    expectArithmeticTrap(Prog{}.I(1).I(0).emit(0x24), "divide by zero");
    expectArithmeticTrap(Prog{}.I(1).I(0).emit(0x25), "divide by zero");
    expectArithmeticTrap(Prog{}.I(1).I(0).emit(0x26), "divide by zero");
}

TEST_CASE("INT32_MIN / -1 traps as integer overflow") {
    expectArithmeticTrap(Prog{}.I(kMin).I(-1).emit(0x23), "integer overflow");
}

TEST_CASE("i32 bitwise operations") {
    REQUIRE(evalI32(Prog{}.I(0x0000F0F0).I(0x0000FF00).emit(0x27)) == 0x0000F000);
    REQUIRE(evalI32(Prog{}.I(0x00000F0F).I(0x0000F0F0).emit(0x28)) == 0x0000FFFF);
    REQUIRE(evalI32(Prog{}.I(0x0000FFFF).I(0x00000FF0).emit(0x29)) == 0x0000F00F);
}

TEST_CASE("i32 shifts mask the amount and match pinned semantics") {
    REQUIRE(evalI32(Prog{}.I(1).I(31).emit(0x2A)) == kMin);
    REQUIRE(evalI32(Prog{}.I(1).I(33).emit(0x2A)) == 2); // 33 & 31 = 1
    REQUIRE(evalI32(Prog{}.I(8).I(1).emit(0x2B)) == 4);
    REQUIRE(evalI32(Prog{}.I(-8).I(1).emit(0x2B)) == -4); // arithmetic shift
    REQUIRE(evalI32(Prog{}.I(-1).I(31).emit(0x2B)) == -1);
    REQUIRE(evalI32(Prog{}.I(kMin).I(1).emit(0x2C)) == 0x40000000); // logical
    REQUIRE(evalI32(Prog{}.I(-1).I(31).emit(0x2C)) == 1);
}

TEST_CASE("i32 neg and not wrap") {
    REQUIRE(evalI32(Prog{}.I(5).emit(0x2D)) == -5);
    REQUIRE(evalI32(Prog{}.I(kMin).emit(0x2D)) == kMin); // wraps
    REQUIRE(evalI32(Prog{}.I(0).emit(0x2E)) == -1);
    REQUIRE(evalI32(Prog{}.I(0x00000F0F).emit(0x2E)) == static_cast<i32>(0xFFFFF0F0u));
}

TEST_CASE("i32 comparisons, signed and unsigned") {
    REQUIRE(evalI32(Prog{}.I(5).I(5).emit(0x30)) == 1);
    REQUIRE(evalI32(Prog{}.I(5).I(6).emit(0x31)) == 1);
    REQUIRE(evalI32(Prog{}.I(-1).I(1).emit(0x32)) == 1); // lt_s
    REQUIRE(evalI32(Prog{}.I(-1).I(1).emit(0x33)) == 0); // lt_u: 0xFFFFFFFF > 1
    REQUIRE(evalI32(Prog{}.I(2).I(1).emit(0x34)) == 1); // gt_s
    REQUIRE(evalI32(Prog{}.I(-1).I(1).emit(0x35)) == 1); // gt_u
    REQUIRE(evalI32(Prog{}.I(5).I(5).emit(0x36)) == 1); // le_s
    REQUIRE(evalI32(Prog{}.I(1).I(-1).emit(0x37)) == 1); // le_u: 1 <= 0xFFFFFFFF
    REQUIRE(evalI32(Prog{}.I(5).I(5).emit(0x38)) == 1); // ge_s
    REQUIRE(evalI32(Prog{}.I(-1).I(1).emit(0x39)) == 1); // ge_u
}

TEST_CASE("f32 arithmetic is IEEE binary32") {
    REQUIRE(evalF32(Prog{}.F(1.5f).F(2.25f).emit(0x40)) == 3.75f);
    REQUIRE(evalF32(Prog{}.F(5.f).F(2.f).emit(0x41)) == 3.f);
    REQUIRE(evalF32(Prog{}.F(2.5f).F(4.f).emit(0x42)) == 10.f);
    REQUIRE(evalF32(Prog{}.F(10.f).F(4.f).emit(0x43)) == 2.5f);
    REQUIRE(evalF32(Prog{}.F(-1.5f).emit(0x4A)) == 1.5f);
    REQUIRE(evalF32(Prog{}.F(-2.5f).emit(0x4B)) == 2.5f);
    REQUIRE(evalF32(Prog{}.F(16.f).emit(0x4C)) == 4.f);
}

TEST_CASE("f32 multiplication is not contracted into an FMA") {
    // (1 + 2^-23)^2 - (1 + 2^-22): the exact result is 2^-46. Fused, the
    // product keeps its low bit and the subtraction yields 2^-46; unfused,
    // the product rounds to 1 + 2^-22 and the subtraction yields 0.
    const f32 a = 1.0f + 1.0f / 8388608.0f;  // 1 + 2^-23
    const f32 b = a;
    const f32 c = -(1.0f + 1.0f / 4194304.0f); // -(1 + 2^-22)
    const f32 unfused = (a * b) + c;
    const f32 fused = std::fma(a, b, c);
    // The vector must actually be contraction-sensitive, or this test is
    // vacuous.
    REQUIRE(bits(unfused) != bits(fused));
    REQUIRE(unfused == 0.f);
    REQUIRE(bits(fused) == 0x28800000u); // 2^-46 in binary32

    const auto result = evalF32(Prog{}.F(a).F(b).emit(0x42).F(c).emit(0x40));
    REQUIRE(bits(result) == bits(unfused));
}

TEST_CASE("f32 comparisons follow IEEE predicates") {
    REQUIRE(evalI32(Prog{}.F(1.f).F(1.f).emit(0x44)) == 1);
    REQUIRE(evalI32(Prog{}.F(1.f).F(2.f).emit(0x45)) == 1);
    REQUIRE(evalI32(Prog{}.F(1.f).F(2.f).emit(0x46)) == 1);
    REQUIRE(evalI32(Prog{}.F(2.f).F(1.f).emit(0x47)) == 1);
    REQUIRE(evalI32(Prog{}.F(1.f).F(1.f).emit(0x48)) == 1);
    REQUIRE(evalI32(Prog{}.F(1.f).F(1.f).emit(0x49)) == 1);

    const f32 nan = std::numeric_limits<f32>::quiet_NaN();
    REQUIRE(evalI32(Prog{}.F(nan).F(nan).emit(0x44)) == 0); // NaN != NaN via eq
    REQUIRE(evalI32(Prog{}.F(nan).F(nan).emit(0x45)) == 1); // ne is true for NaN
    REQUIRE(evalI32(Prog{}.F(nan).F(1.f).emit(0x46)) == 0); // ordering false
}

TEST_CASE("conversions follow the pinned rules") {
    REQUIRE(evalF32(Prog{}.I(3).emit(0x4D)) == 3.f);
    REQUIRE(evalF32(Prog{}.I(-3).emit(0x4D)) == -3.f);

    REQUIRE(evalI32(Prog{}.F(3.7f).emit(0x4E)) == 3);   // truncate
    REQUIRE(evalI32(Prog{}.F(-3.7f).emit(0x4E)) == -3); // truncate toward zero
    const f32 nan = std::numeric_limits<f32>::quiet_NaN();
    REQUIRE(evalI32(Prog{}.F(nan).emit(0x4E)) == 0);            // NaN -> 0
    const f32 inf = std::numeric_limits<f32>::infinity();
    REQUIRE(evalI32(Prog{}.F(inf).emit(0x4E)) == kMax);         // +inf saturates
    REQUIRE(evalI32(Prog{}.F(-inf).emit(0x4E)) == kMin);        // -inf saturates
    REQUIRE(evalI32(Prog{}.F(1.0e20f).emit(0x4E)) == kMax);     // out of range
}

TEST_CASE("color.shade clamps channels and keeps alpha") {
    const Color c{100, 150, 200, 255};
    REQUIRE(evalColor(Prog{}.C(c).I(50).emit(0x51)) == Color{150, 200, 250, 255});
    REQUIRE(evalColor(Prog{}.C(c).I(-300).emit(0x51)) == Color{0, 0, 0, 255});
    const Color semi{10, 20, 30, 77};
    REQUIRE(evalColor(Prog{}.C(semi).I(5).emit(0x51)) == Color{15, 25, 35, 77});
}

TEST_CASE("color.mix lerps RGB, clamps t and copies the first alpha") {
    const Color a{0, 0, 0, 255};
    const Color b{100, 200, 50, 255};
    REQUIRE(evalColor(Prog{}.C(a).C(b).F(0.5f).emit(0x50)) == Color{50, 100, 25, 255});
    REQUIRE(evalColor(Prog{}.C(a).C(b).F(2.f).emit(0x50)) == Color{100, 200, 50, 255}); // t clamped
    REQUIRE(evalColor(Prog{}.C(a).C(b).F(-1.f).emit(0x50)) == Color{0, 0, 0, 255});      // t clamped

    const Color src{1, 2, 3, 7};
    const Color other{9, 9, 9, 9};
    REQUIRE(evalColor(Prog{}.C(src).C(other).F(0.f).emit(0x50)) == Color{1, 2, 3, 7});
}
