#include <catch2/catch_test_macros.hpp>

#include <array>
#include <bit>
#include <cmath>
#include <limits>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "Compiler.hpp"
#include "iprop/Debug.hpp"
#include "iprop/Instruction.hpp"
#include "iprop/VM.hpp"
#include "ips/Parser.hpp"
#include "ips/Printer.hpp"
#include "ips/Semantics.hpp"
#include "ips/SourceError.hpp"

#include "support/compiler_support.hpp"
#include "support/test_support.hpp"

// Uniform random ranges (spec v0.5 §3). The pinned vectors were computed from
// the spec formulas by an independent reference program.

using namespace iprop;

namespace {
constexpr u8 kStreamPush = 0x60, kStreamState = 0x66, kRngInt = 0xA8, kRngFloat = 0xA9;
constexpr i32 kMin = std::numeric_limits<i32>::min(), kMax = std::numeric_limits<i32>::max();

struct Draw {
    std::vector<Value> stack;
    u64 spent = 0;
};

Draw run(const test_support::Prog& prog) {
    const auto bytes = test_support::buildProgFile(prog);
    const auto loaded = loadFile(bytes);
    VM vm(loaded, {});
    vm.run();
    return {vm.debugStack(), vm.fuelBudget() - vm.fuel()};
}

// stream.push(seed); range; stream.state — six instructions plus redraws.
test_support::Prog intRange(u32 seed, i32 lo, i32 hi) {
    test_support::Prog prog;
    prog.I(static_cast<i32>(seed)).emit(kStreamPush).I(lo).I(hi).emit(kRngInt).emit(kStreamState);
    return prog;
}

test_support::Prog floatRange(u32 seed, f32 lo, f32 hi) {
    test_support::Prog prog;
    prog.I(static_cast<i32>(seed)).emit(kStreamPush).F(lo).F(hi).emit(kRngFloat).emit(kStreamState);
    return prog;
}

struct IntVector {
    u32 seed;
    i32 lo, hi, value;
    u32 state, draws;
};

std::vector<u8> compileSource(const std::string& source, bool optimize) {
    const auto program = ips::parse(source);
    ips::checkAsset(program, {});
    return compileProgram(program, {}, optimize);
}

std::vector<u32> pointBits(const std::vector<u8>& bytes, u32 seed, u32 variant = 0) {
    const auto loaded = loadFile(bytes);
    validate(loaded);
    VM vm(loaded, GenerationInputs{seed, variant, 0, 0, 0, {}});
    vm.run();
    std::vector<u32> bits;
    for (const auto& output : vm.result().outputs)
        for (const auto& point : output.points) {
            bits.push_back(std::bit_cast<u32>(point.x));
            bits.push_back(std::bit_cast<u32>(point.y));
        }
    return bits;
}

std::string sourceError(const std::string& source) {
    try {
        ips::checkAsset(ips::parse(source), {});
    } catch (const ips::SourceError& error) {
        return error.message;
    }
    FAIL("expected a source error");
    return {};
}

std::string format(const std::string& source) { return ips::formatProgram(ips::parse(source)); }

bool usesOpcode(const std::vector<u8>& bytes, Opcode opcode) {
    for (const auto& function : loadFile(bytes).code.functions)
        for (std::size_t i = 0; i < function.code.size() / 4; ++i)
            if (decodeInstruction(function.code.subspan(i * 4, 4)).opcode == opcode) return true;
    return false;
}

// Pearson chi-square of the counts against a uniform expectation.
double chiSquare(const std::vector<u32>& counts) {
    double total = 0;
    for (const auto count : counts) total += count;
    const double expected = total / static_cast<double>(counts.size());
    double sum = 0;
    for (const auto count : counts) sum += (count - expected) * (count - expected) / expected;
    return sum;
}
} // namespace

TEST_CASE("rng.int matches the pinned rejection vectors, stream states and fuel") {
    const IntVector vectors[] = {
        {0, -5, 5, 1, 0x9e3779b9u, 1},
        {0, 0, 255, 82, 0x9e3779b9u, 1},
        {0, kMin, kMax, -2114132654, 0x9e3779b9u, 1},
        {0, 3, 3, 3, 0x9e3779b9u, 1},
        {7, -5, 5, 3, 2654435776u, 1},
        {7, 0, 255, 219, 2654435776u, 1},
        {7, kMin, kMax, -1076253733, 2654435776u, 1},
        {7, 1, 6, 2, 2654435776u, 1},
        {42, -5, 5, 4, 2654435811u, 1},
        {42, 0, 255, 85, 2654435811u, 1},
        {42, kMin, kMax, 1850113877, 2654435811u, 1},
        {42, kMin, 0, -273537241, 387276999u, 5},
        {0xffffffffu, kMin, 0, -1608904676, 1013904241u, 2},
        // The longest rejection run of the whole 2^32-state cycle, for the
        // largest rejection set: 30 rejected draws, then an accepted one.
        {0x67694290u, kMin, 0, -2035857874, 2418081783u, 31},
    };
    for (const auto& vector : vectors) {
        INFO(vector.seed << " " << vector.lo << " to " << vector.hi);
        const auto draw = run(intRange(vector.seed, vector.lo, vector.hi));
        REQUIRE(draw.stack.size() == 2);
        CHECK(std::get<i32>(draw.stack[0]) == vector.value);
        CHECK(static_cast<u32>(std::get<i32>(draw.stack[1])) == vector.state);
        CHECK(draw.spent == 5 + vector.draws); // 6 instructions, the first draw included
    }
}

TEST_CASE("rng.float matches pinned vectors and stays inside its bounds") {
    const std::array<std::tuple<u32, f32, f32, u32>, 7> vectors{{
        {0, 1.6f, 2.0f, 0x3fcd3294u}, {0, -1.f, 1.f, 0xbf7c0636u}, {0, 0.1f, 0.1f, 0x3dcccccdu},
        {7, 1.6f, 2.0f, 0x3fd991efu}, {7, -1.f, 1.f, 0xbf004caau},
        {42, 1.6f, 2.0f, 0x3ffc7480u}, {42, -1.f, 1.f, 0x3f5c8cfeu}}};
    for (const auto& [seed, lo, hi, bits] : vectors) {
        INFO(seed << " " << lo << " to " << hi);
        const auto draw = run(floatRange(seed, lo, hi));
        CHECK(test_support::bitsOf(std::get<f32>(draw.stack[0])) == bits);
        CHECK(static_cast<u32>(std::get<i32>(draw.stack[1])) == seed + 0x9e3779b9u);
        CHECK(draw.spent == 6);
    }
    constexpr f32 big = std::numeric_limits<f32>::max(), tiny = std::numeric_limits<f32>::denorm_min();
    const std::array<std::pair<f32, f32>, 6> bounds{{
        {-big, big}, {big, big}, {-big, -big}, {0.1f, 0.1f}, {tiny, tiny}, {-tiny, tiny}}};
    for (const auto& [lo, hi] : bounds) {
        for (u32 seed = 0; seed < 64; ++seed) {
            INFO(lo << " to " << hi << " seed " << seed);
            const f32 value = std::get<f32>(run(floatRange(seed * 0x10001u, lo, hi)).stack[0]);
            CHECK(std::isfinite(value));
            CHECK(value >= lo);
            CHECK(value <= hi);
            if (lo == hi) CHECK(test_support::bitsOf(value) == test_support::bitsOf(lo));
        }
    }
}

TEST_CASE("random ranges trap on reversed or non-finite bounds") {
    using test_support::expectProgTrap;
    using test_support::Prog;
    expectProgTrap(Prog{}.I(5).I(4).emit(kRngInt), "lower bound exceeds the upper bound");
    expectProgTrap(Prog{}.I(kMax).I(kMin).emit(kRngInt), "lower bound exceeds the upper bound");
    expectProgTrap(Prog{}.F(2.f).F(1.f).emit(kRngFloat), "lower bound exceeds the upper bound");
    constexpr f32 inf = std::numeric_limits<f32>::infinity(), nan = std::numeric_limits<f32>::quiet_NaN();
    for (const auto& [lo, hi] : std::array<std::pair<f32, f32>, 4>{{{nan, 1.f}, {0.f, nan}, {-inf, 0.f}, {0.f, inf}}})
        expectProgTrap(Prog{}.F(lo).F(hi).emit(kRngFloat), "bounds must be finite");
}

TEST_CASE("integer ranges are uniform, including widths that need rejection") {
    // Without rejection, x mod 3*2^29 would favour the first third 3:2.
    const std::array<std::pair<i32, i32>, 3> ranges{{{-5, 5}, {0, 2}, {0, 3 * (1 << 29) - 1}}};
    for (const auto& [lo, hi] : ranges) {
        INFO(lo << " to " << hi);
        const u64 width = static_cast<u64>(static_cast<i64>(hi) - lo) + 1;
        const u32 bins = width <= 11 ? static_cast<u32>(width) : 3;
        std::vector<u32> counts(bins);
        test_support::Prog prog;
        prog.I(12345).emit(kStreamPush);
        for (int batch = 0; batch < 200; ++batch) {
            test_support::Prog step = prog;
            for (int i = 0; i < 100; ++i) step.I(lo).I(hi).emit(kRngInt);
            step.emit(kStreamState);
            const auto stack = run(step).stack;
            for (int i = 0; i < 100; ++i) {
                const u64 offset = static_cast<u64>(static_cast<i64>(std::get<i32>(stack[i])) - lo);
                ++counts[static_cast<std::size_t>(offset * bins / width)];
            }
            prog = test_support::Prog{};
            prog.I(std::get<i32>(stack.back())).emit(kStreamPush);
        }
        // p = 0.001 critical values for 2 and 10 degrees of freedom.
        CHECK(chiSquare(counts) < (bins == 3 ? 13.82 : 29.59));
    }
}

TEST_CASE("'to' types its result from the bounds and draws in source order") {
    const std::string source = R"(asset t {
  size 1x1
  output list point p
  draw {
    for i in 0..64 {
      offset = -5 to 5
      offset = 7
      thickness = 1.6 to 2.0
      mixed = 1 to 2.0
      p = {offset, thickness}
      p = {mixed, base(i) - 2 to base(i) + 2}
      p = {(1 to 3) to (5 to 8), 0}
    }
  }
  base(i: i32) -> i32 { return i * 3 }
})";
    for (const bool optimize : {false, true}) {
        const auto bytes = compileSource(source, optimize);
        CHECK(bytes[6] == 5);
        CHECK(usesOpcode(bytes, Opcode::RngInt));
        CHECK(usesOpcode(bytes, Opcode::RngFloat));
        const auto loaded = loadFile(bytes);
        VM vm(loaded, GenerationInputs{9, 0, 0, 0, 0, {}});
        vm.run();
        const auto points = vm.result().outputs[0].points;
        REQUIRE(points.size() == 3 * 64);
        for (u32 i = 0; i < 64; ++i) {
            INFO(i);
            CHECK(points[3 * i].x == 7.f);
            CHECK(points[3 * i].y >= 1.6f);
            CHECK(points[3 * i].y <= 2.f);
            CHECK(points[3 * i + 1].x >= 1.f);
            CHECK(points[3 * i + 1].x <= 2.f);
            CHECK(points[3 * i + 1].y >= static_cast<f32>(3 * i) - 2.f);
            CHECK(points[3 * i + 1].y <= static_cast<f32>(3 * i) + 2.f);
            CHECK(points[3 * i + 2].x >= 1.f);
            CHECK(points[3 * i + 2].x <= 8.f);
        }
    }
    CHECK(sourceError("asset t { size 1x1 draw { x = 0 to 5 x = 1.5 } }").find("cannot assign f32") !=
          std::string::npos);
    CHECK(sourceError("asset t { size 1x1 draw { x = #000 to 5 } }") ==
          "'to' needs two numbers, two colors or two vec2 values, got color and i32");
    CHECK(sourceError("asset t { size 1x1 draw { x = {0, 0} to 5 } }") ==
          "'to' needs two numbers, two colors or two vec2 values, got vec2 and i32");
}

TEST_CASE("bounds evaluate once, lower first, before the draw") {
    const auto program = [](const std::string& body) {
        return "asset t { size 4x1 output list point p\n"
               "  f(x: i32) -> i32 { pixel(x, 0, #fff) return x + index(10) }\n"
               "  draw { " + body + " p = {index(1000), unit()} } }";
    };
    const std::vector<std::pair<std::string, std::string>> equivalents = {
        {"x = f(0) to f(1) + 20 p = {x, 0}", "l = f(0) u = f(1) + 20 x = l to u p = {x, 0}"},
        {"x = (0 to 3) to (10 to 13) p = {x, 0}", "l = 0 to 3 u = 10 to 13 x = l to u p = {x, 0}"},
        {"x = 0.5 to f(2) p = {x, 0}", "u = f(2) x = 0.5 to u p = {x, 0}"},
        {"dead = 0 to 9 p = {0 to 9, 0 to 9}", "dead = 0 to 9 first = 0 to 9 second = 0 to 9 p = {first, second}"},
    };
    for (const auto& [direct, stored] : equivalents) {
        INFO(direct);
        for (const bool optimize : {false, true})
            for (const u32 seed : {0u, 7u, 26u, 0xffffffffu})
                CHECK(pointBits(compileSource(program(direct), optimize), seed) ==
                      pointBits(compileSource(program(stored), false), seed));
    }
    // An unused draw still advances the stream, and two equal ranges are two draws.
    const auto removed = compileSource(program("p = {0 to 9, 0 to 9}"), true);
    CHECK(pointBits(removed, 7) != pointBits(compileSource(program("dead = 0 to 9 p = {0 to 9, 0 to 9}"), true), 7));
    const auto stack = run(test_support::Prog{}.I(3).emit(kStreamPush).I(4).I(4).emit(kRngInt).emit(kStreamState)).stack;
    CHECK(std::get<i32>(stack[0]) == 4);
    CHECK(static_cast<u32>(std::get<i32>(stack[1])) == 3 + 0x9e3779b9u);
}

TEST_CASE("runtime traps keep their reason through compiled ranges") {
    const std::string reversed = "asset t { size 1x1 variants 2 output point p draw { p = {v to 0, 0} } }";
    const std::string nonfinite = "asset t { size 1x1 output point p draw { z = 0.0 p = {z / z to 1.0, 0} } }";
    for (const bool optimize : {false, true}) {
        CHECK(pointBits(compileSource(reversed, optimize), 0, 0) == std::vector<u32>{0, 0});
        CHECK_THROWS_AS(pointBits(compileSource(reversed, optimize), 0, 1), Trap);
        CHECK_THROWS_AS(pointBits(compileSource(nonfinite, optimize), 0), Trap);
    }
}

TEST_CASE("'to' binds between shifts and comparisons and cannot be chained") {
    CHECK(format("asset t { size 1x1 draw { a = 1 b = 2 x = a + 1 to b * 2 } }").find("x = ((a + 1) to (b * 2))") !=
          std::string::npos);
    CHECK(format("asset t { size 1x1 draw { x = 1 << 2 to 3 >> 1 } }").find("x = ((1 << 2) to (3 >> 1))") !=
          std::string::npos);
    CHECK(format("asset t { size 1x1 draw { x = 0 to 9 < 5 } }").find("x = ((0 to 9) < 5)") != std::string::npos);
    CHECK(format("asset t { size 1x1 draw { n = 1 x = n == 0 to 1 } }").find("x = (n == (0 to 1))") !=
          std::string::npos);
    CHECK(format("asset t { size 1x1 draw { x = -5 to 5 } }").find("x = ((-5) to 5)") != std::string::npos);
    const std::string grouped = "asset t { size 1x1 draw { x = (1 to 3) to (5 to 8) } }";
    const auto formatted = format(grouped);
    CHECK(formatted.find("x = ((1 to 3) to (5 to 8))") != std::string::npos);
    CHECK(format(formatted) == formatted);
    CHECK(stripDebug(compileSource(formatted, false)) == stripDebug(compileSource(grouped, false)));
    try {
        (void)ips::parse("asset t { size 1x1 draw { x = 1 to 2 to 3 } }");
        FAIL("expected a parse error");
    } catch (const ips::SourceError& error) {
        CHECK(error.message == "'to' cannot be chained; parenthesize the range used as a bound, for example "
                               "(a to b) to c");
        CHECK(error.location.column == 38); // the second 'to'
    }
}

TEST_CASE("existing names 'to' keep their meaning next to the operator") {
    // A local named `to`, read as a bound and assigned after a statement.
    const std::string local = R"(asset t { size 1x1 output list point p
      draw { to = 3 x = to + 1 p = {x, to} y = 1 to = 5 p = {y, to} z = to to to + 2 p = {z, 0} } })";
    const auto localBits = pointBits(compileSource(local, false), 4);
    REQUIRE(localBits.size() == 6);
    CHECK(localBits[0] == std::bit_cast<u32>(4.f));
    CHECK(localBits[2] == std::bit_cast<u32>(1.f));
    CHECK(localBits[3] == std::bit_cast<u32>(5.f));
    // A helper named `to`: without a separator its call is the next statement;
    // inside parentheses or arguments, `to` is the operator.
    const std::string helper = R"(asset t { size 8x1 output list point p
      to(x: i32) -> i32 { pixel(x, 0, #fff) return x }
      draw {
        x = 1 to(2) p = {x, 0}
        y = (1 to (5)) p = {y, 0}
        p = {to(6), 3 to (3)}
        for i in 0..to(1) { p = {i, 0} }
      } })";
    for (const bool optimize : {false, true}) {
        const auto bytes = compileSource(helper, optimize);
        const auto loaded = loadFile(bytes);
        VM vm(loaded, GenerationInputs{0, 0, 0, 0, 0, {}});
        vm.run();
        const auto result = vm.result();
        const auto& points = result.outputs[0].points;
        REQUIRE(points.size() == 4);
        CHECK(points[0].x == 1.f);
        CHECK(points[1].x >= 1.f);
        CHECK(points[1].x <= 5.f);
        CHECK(points[2].x == 6.f);
        CHECK(points[2].y == 3.f);
        CHECK(points[3].x == 0.f);
        for (const u32 x : {2u, 6u, 1u}) CHECK(result.image.at(x, 0) == Color{255, 255, 255, 255});
        CHECK(result.image.at(0, 0).a == 0);
    }
    // A part alias `to` starts its draw statement after an assignment.
    const auto program = ips::parse(R"(import to from "leaf.ips"
      asset t { size 1x1 draw { x = 1 to.draw(seed, 0, 0, at = {0, 0}) } })");
    const auto& body = program.asset.functions.at(0).body;
    REQUIRE(body.size() == 2);
    CHECK(std::holds_alternative<ips::Literal>(*std::get<ips::VarDecl>(body[0]).value));
    CHECK(std::holds_alternative<ips::ExprStmt>(body[1]));
    for (const auto& source : {local, helper}) {
        const auto formatted = format(source);
        CHECK(format(formatted) == formatted);
        CHECK(stripDebug(compileSource(formatted, false)) == stripDebug(compileSource(source, false)));
    }
}
