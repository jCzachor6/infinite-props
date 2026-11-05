#include <catch2/catch_test_macros.hpp>

#include "Compiler.hpp"
#include "iprop/VM.hpp"
#include "ips/Parser.hpp"
#include "ips/Printer.hpp"
#include "ips/Semantics.hpp"
#include "support/compiler_support.hpp"
#include "support/math_support.hpp"

#include <bit>
#include <algorithm>
#include <limits>

using namespace iprop;
using compiler_test::compileChecked;
using namespace test_support;

namespace {
constexpr auto cell2 = static_cast<u8>(Opcode::NoiseCell2);
constexpr auto cellId = static_cast<u8>(Opcode::NoiseCellId);
constexpr auto warpVec = static_cast<u8>(Opcode::NoiseWarpVec);
constexpr auto fbmConfig = static_cast<u8>(Opcode::NoiseFbmConfig);

Prog cellProgram(u8 op, f32 x, f32 y, i32 seed = 7) {
    return Prog{}.F(x).F(y).I(seed).emit(op);
}
Prog fbmProgram(f32 x, f32 y, i32 n, f32 lac, f32 gain, i32 seed = 7) {
    return Prog{}.F(x).F(y).I(n).F(lac).F(gain).I(seed).emit(fbmConfig);
}
Prog warpProgram(f32 x, f32 y, f32 amount, i32 seed = 7) {
    return Prog{}.F(x).F(y).F(amount).I(seed).emit(warpVec);
}
void numericTrap(Prog program, const char* message) {
    program.emit(static_cast<u8>(Opcode::Drop));
    const auto bytes = buildProgFile(program);
    const auto file = loadFile(bytes);
    REQUIRE_NOTHROW(validate(file));
    VM vm(file, {});
    try { vm.run(); FAIL("invalid noise input accepted"); }
    catch (const Trap& error) {
        CHECK(error.reason == ErrorReason::NumericRange);
        CHECK(std::string(error.what()).find(message) != std::string::npos);
        CHECK(error.stage == ErrorStage::Execute);
        CHECK((error.location.flags & ErrorLocation::PC) != 0);
    }
    CHECK_THROWS_AS(vm.takeResult(), std::logic_error);
}
u32 hash(u32 x) {
    x ^= x >> 16; x *= 0x7feb352d; x ^= x >> 15; x *= 0x846ca68b;
    return x ^ (x >> 16);
}
} // namespace

TEST_CASE("noise extensions preserve F1 and resolve the frozen Worley ties", "[noise-v09]") {
    struct Tie { f32 x, y; i32 seed; u32 distance; };
    // Exact witnesses captured before cell_id existed (compat-v0.8.json).
    for (const auto& t : {Tie{0.9841634631156921f, 0.5463124513626099f, 1, 1058696814},
                          Tie{1.0360546112060547f, 0.24961066246032715f, 5, 1060418595},
                          Tie{0.9441530108451843f, 0.4423506259918213f, 22, 1053651649}}) {
        CHECK(bitsOf(evalProgF32(cellProgram(0x8B, t.x, t.y, t.seed))) == t.distance);
        CHECK(bitsOf(evalProgF32(cellProgram(cell2, t.x, t.y, t.seed))) == t.distance);
        // The tied cells are (0,0) and (1,0); the first must own the ID.
        CHECK(std::bit_cast<u32>(evalProgI32(cellProgram(cellId, t.x, t.y, t.seed))) == hash(static_cast<u32>(t.seed)));
    }
    for (u32 i = 0; i < 500; ++i) {
        const f32 x = math_test::unitFromBits(i * 2654435761u) * 32.f - 16.f;
        const f32 y = math_test::unitFromBits(i * 2246822519u + 3266489917u) * 32.f - 16.f;
        CHECK(evalProgF32(cellProgram(cell2, x, y)) >= evalProgF32(cellProgram(0x8B, x, y)));
    }
    for (const auto op : {cell2, cellId}) {
        CHECK_NOTHROW(evalProg(cellProgram(op, -16777214.f, 16777214.f)));
        for (const f32 bad : {-16777215.f, 16777215.f, std::numeric_limits<f32>::infinity(),
                              -std::numeric_limits<f32>::infinity(), f32FromBits(0x7fc01234)}) {
            numericTrap(cellProgram(op, bad, 0.f), "noise coordinate out of range");
            numericTrap(cellProgram(op, 0.f, bad), "noise coordinate out of range");
        }
    }
}

TEST_CASE("configured fBm uses pinned gradient samples and skips only gain-zero octaves", "[noise-v09]") {
    for (const i32 n : {-2147483647 - 1, -3, 0, 1, 2, 4, 8, 9, 2147483647}) {
        for (const f32 lac : {f32FromBits(1), 0.25f, 1.f, 2.f, 8.f}) {
            for (const f32 gain : {-0.f, 0.5f, 1.f}) {
                const i32 count = gain == 0.f ? 1 : std::clamp(n, 1, 8);
                f32 sum = 0.f, total = 0.f, amp = 1.f, freq = 1.f;
                for (i32 i = 0; i < count; ++i) {
                    const auto seed = std::bit_cast<i32>(7u + static_cast<u32>(i) * 0x9e3779b9u);
                    const f32 sample = evalProgF32(Prog{}.F(0.375f * freq).F(-0.625f * freq).I(seed).emit(0x89));
                    sum += amp * sample;
                    total += amp;
                    amp *= gain; freq *= lac;
                }
                CHECK(bitsOf(evalProgF32(fbmProgram(0.375f, -0.625f, n, lac, gain))) == bitsOf(sum / total));
            }
        }
        CHECK(bitsOf(evalProgF32(fbmProgram(0.375f, -0.625f, n, 2.f, 0.5f))) ==
              bitsOf(evalProgF32(Prog{}.F(0.375f).F(-0.625f).I(n).I(7).emit(0x8A))));
    }
    const f32 inf = std::numeric_limits<f32>::infinity(), nan = f32FromBits(0x7fc01234);
    for (const f32 bad : {-1.f, -0.f, 0.f, std::nextafter(8.f, inf), inf, -inf, nan})
        numericTrap(fbmProgram(0.f, 0.f, 1, bad, 0.f), "fbm lacunarity out of range");
    for (const f32 bad : {-f32FromBits(1), std::nextafter(1.f, inf), inf, -inf, nan})
        numericTrap(fbmProgram(0.f, 0.f, 1, 2.f, bad), "fbm gain out of range");
    for (const f32 bad : {inf, -inf, nan, 2147483648.f, -2147483904.f}) {
        numericTrap(fbmProgram(bad, 0.f, 8, 8.f, 0.f), "noise coordinate out of range");
        numericTrap(fbmProgram(0.f, bad, 8, 8.f, 0.f), "noise coordinate out of range");
    }
    CHECK_NOTHROW(evalProg(fbmProgram(1073741824.f, 0.f, 8, 8.f, 0.f)));
    numericTrap(fbmProgram(1073741824.f, 0.f, 2, 2.f, 0.5f), "noise coordinate out of range");
    // Parameter validation precedes sampling, even on the gain-zero shortcut.
    numericTrap(fbmProgram(nan, inf, 0, 0.f, nan), "fbm lacunarity out of range");
    numericTrap(fbmProgram(nan, inf, 0, 2.f, nan), "fbm gain out of range");
}

TEST_CASE("warp_vec reuses legacy inner fields and validates its zero shortcut", "[noise-v09]") {
    for (const auto p : {Vec2{0.5f, 0.5f}, Vec2{-3.25f, 4.75f}, Vec2{0.f, -0.f}}) {
        const f32 qx = evalProgF32(Prog{}.F(p.x).F(p.y).I(4).I(7).emit(0x8A));
        const f32 qy = evalProgF32(Prog{}.F(p.x + 3.7f).F(p.y + 9.1f).I(4).I(7).emit(0x8A));
        for (const f32 amount : {-2.f, 0.2f, 3.f}) {
            const auto actual = std::get<Vec2>(evalProg(warpProgram(p.x, p.y, amount))[0]);
            CHECK(bitsOf(actual.x) == bitsOf(p.x + amount * qx));
            CHECK(bitsOf(actual.y) == bitsOf(p.y + amount * qy));
        }
    }
    for (const auto p : {Vec2{-0.f, -0.f}, Vec2{0.f, -0.f}, Vec2{0.f, 268435440.f},
                         Vec2{-2147483648.f, 2147483520.f}}) {
        for (const f32 zero : {0.f, -0.f}) {
            const auto actual = std::get<Vec2>(evalProg(warpProgram(p.x, p.y, zero))[0]);
            CHECK(bitsOf(actual.x) == bitsOf(p.x));
            CHECK(bitsOf(actual.y) == bitsOf(p.y));
        }
    }
    const f32 inf = std::numeric_limits<f32>::infinity(), nan = f32FromBits(0x7fc01234);
    for (const f32 bad : {inf, -inf, nan, 2147483648.f, -2147483904.f}) {
        numericTrap(warpProgram(bad, 0.f, 0.f), "noise coordinate out of range");
        numericTrap(warpProgram(0.f, bad, -0.f), "noise coordinate out of range");
    }
    for (const f32 bad : {inf, -inf, nan})
        numericTrap(warpProgram(nan, inf, bad), "warp amount out of range");
    numericTrap(warpProgram(0.f, 268435440.f, 0.2f), "noise coordinate out of range");
    numericTrap(warpProgram(0.5f, 0.5f, std::numeric_limits<f32>::max()), "noise coordinate out of range");
}

TEST_CASE("new noise reserves its complete fuel before sampling and preserves RNG", "[noise-v09]") {
    std::vector<std::pair<Prog, u64>> cases{
        {cellProgram(cell2, 0.5f, 0.5f), 24}, {cellProgram(cellId, 0.5f, 0.5f), 24},
        {warpProgram(0.5f, 0.5f, 0.2f), 7}, {warpProgram(-0.f, 268435440.f, 0.f), 0},
        {cellProgram(0x8B, 0.5f, 0.5f), 0}};
    for (const i32 n : {-3, 0, 1, 2, 8, 9}) for (const f32 gain : {0.f, 0.5f, 1.f})
        cases.emplace_back(fbmProgram(0.5f, 0.5f, n, 2.f, gain), static_cast<u64>(std::clamp(n, 1, 8) - 1));
    for (auto& [program, extra] : cases) {
        const u64 exact = program.body.size() / 4 + extra;
        const auto bytes = buildProgFile(program);
        auto file = loadFile(bytes);
        file.meta.fuel = exact;
        VM vm(file, {});
        REQUIRE_NOTHROW(vm.run());
        CHECK(vm.fuel() == 0);
        file.meta.fuel = exact - 1;
        VM low(file, {});
        try { low.run(); FAIL("insufficient fuel accepted"); }
        catch (const Trap& error) { CHECK(error.reason == ErrorReason::FuelExhausted); }
        CHECK_THROWS_AS(low.takeResult(), std::logic_error);
        program.emit(static_cast<u8>(Opcode::Drop)).emit(static_cast<u8>(Opcode::StreamState));
        CHECK(evalProgI32(program) == 0); // default seed, no stream draws
    }
    // Invalid first samples distinguish an up-front reservation from billing
    // after work. The reservation fails before the numeric check can execute.
    for (auto program : {cellProgram(cell2, std::numeric_limits<f32>::infinity(), 0.f),
                         fbmProgram(std::numeric_limits<f32>::infinity(), 0.f, 8, 2.f, 1.f),
                         warpProgram(1073741824.f, 0.f, 1.f)}) {
        const auto bytes = buildProgFile(program);
        auto file = loadFile(bytes);
        file.meta.fuel = program.body.size() / 4;
        VM vm(file, {});
        try { vm.run(); FAIL("native work was not reserved"); }
        catch (const Trap& error) { CHECK(error.reason == ErrorReason::FuelExhausted); }
    }
}

TEST_CASE("noise extensions bind named arguments once in signature order and roundtrip formatting", "[noise-v09]") {
    const std::string header = R"(asset t{size 1x1 output list point trace output point value output point state
      point()->vec2{trace={1,unit()} return {unit(),unit()}}
      octaves()->i32{trace={2,unit()} return 4}
      lac()->f32{trace={3,unit()} return 2.0}
      weight()->f32{trace={4,unit()} return 0.5}
      amount()->f32{trace={2,unit()} return 0.2}
      key()->i32{trace={5,unit()} return seed}
      draw{)";
    const std::vector<std::pair<std::string, std::string>> expressions{
        {"n=noise.cell2(seed=key(),p=point()) value={n,0}",
         "p=point() s=key() value={noise.cell2(p.x,p.y,s),0}"},
        {"n=noise.cell_id(seed=key(),p=point()) value={n&65535,n>>16}",
         "p=point() s=key() n=noise.cell_id(p.x,p.y,s) value={n&65535,n>>16}"},
        {"value=noise.warp_vec(seed=key(),amount=amount(),p=point())",
         "p=point() a=amount() s=key() value=noise.warp_vec(p.x,p.y,a,s)"},
        {"value={noise.fbm(gain=weight(),seed=key(),lacunarity=lac(),octaves=octaves(),p=point()),0}",
         "p=point() n=octaves() l=lac() g=weight() s=key() value={noise.fbm(p.x,p.y,n,l,g,s),0}"}};
    for (const auto& [expression, expanded] : expressions) {
        const auto wrap = [&](const std::string& body) { return header + body + " state={unit(),unit()}}}"; };
        const auto expected = compileChecked(wrap(expanded), false);
        for (const bool optimize : {false, true}) {
            const auto formatted = ips::formatProgram(ips::parse(wrap(expression)));
            for (const u32 seed : {0u, 7u, 0xffffffffu}) {
                const auto a = compiler_test::execute(compileChecked(formatted, optimize), seed);
                const auto b = compiler_test::execute(expected, seed);
                REQUIRE(a.outputs.size() == b.outputs.size());
                for (std::size_t i = 0; i < a.outputs.size(); ++i) {
                    REQUIRE(a.outputs[i].points.size() == b.outputs[i].points.size());
                    for (std::size_t j = 0; j < a.outputs[i].points.size(); ++j) {
                        CHECK(bitsOf(a.outputs[i].points[j].x) == bitsOf(b.outputs[i].points[j].x));
                        CHECK(bitsOf(a.outputs[i].points[j].y) == bitsOf(b.outputs[i].points[j].y));
                    }
                }
            }
        }
    }
    for (const auto* expression : {"noise.cell_id({1,2},1.0)", "noise.cell2({1,2},#fff)",
         "noise.warp_vec({1,2},0.5)", "noise.fbm({1,2},2.0,2,0.5,1)",
         "noise.fbm({1,2},octaves=4,lacunarity=2,gain=0.5,seed=1,x=0)"})
        CHECK_THROWS_AS(compileChecked("asset t{size 1x1 draw{" + std::string(expression) + "}}", true), ips::SourceError);
}
