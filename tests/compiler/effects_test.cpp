#include <catch2/catch_test_macros.hpp>

#include <bit>
#include <string>

#include "Compiler.hpp"
#include "iprop/OpcodeSpec.hpp"
#include "ips/BuiltinSpec.hpp"
#include "ips/Parser.hpp"
#include "support/compiler_support.hpp"

using namespace iprop;
using namespace compiler_test;

namespace {
std::string arguments(const ips::BuiltinOverload& overload, bool invalid = false) {
    std::string result;
    bool injected = false;
    for (const auto& parameter : overload.params) {
        if (!result.empty()) result += ',';
        using T = ips::ValueType;
        std::string value = parameter.type == T::Color ? "#999999" :
            parameter.type == T::Vec2 ? "{1,1}" : parameter.type == T::F32 ? "1.0" : "1";
        if (parameter.name == "amount") value = "32";
        if (invalid && !injected && parameter.type != T::Color) {
            value = parameter.type == T::Vec2 ? "{1.0/0.0,1}" :
                parameter.type == T::F32 ? "1.0/0.0" : "2147483647";
            injected = true;
        }
        result += value;
    }
    return result;
}
void expectTrap(const std::vector<u8>& bytes, ErrorReason reason) {
    const auto file = loadFile(bytes);
    REQUIRE_NOTHROW(validate(file));
    try {
        VM vm(file, {});
        vm.run();
        FAIL("advertised runtime check did not trap");
    } catch (const Trap& trap) {
        CHECK(trap.reason == reason);
        CHECK(trap.stage == ErrorStage::Execute);
        CHECK((trap.location.flags & ErrorLocation::PC) != 0);
    }
}
}

TEST_CASE("registry RNG effects agree with every current-stream and seeded builtin overload", "[effects]") {
    const auto reference = compileChecked(R"(asset t {size 8x8 output list point trace
        draw{rect(0,0,8,8,#999999);trace={0,unit()};trace={1,unit()};}
    })");
    const auto advancingReference = compileChecked(R"(asset t {size 8x8 output list point trace
        draw{rect(0,0,8,8,#999999);trace={0,unit()};unit();trace={1,unit()};}
    })");
    for (const auto& [name,spec] : ips::builtinSpecs()) for (const auto& overload : spec.overloads) {
        const auto effects = ips::builtinEffects(spec, &overload);
        if (effects.rng != RngEffect::AdvanceCurrent && effects.rng != RngEffect::SeededComputation) continue;
        INFO(name);
        const auto program = ips::parse("asset t {size 8x8 output list point trace draw{"
            "rect(0,0,8,8,#999999);trace={0,unit()};" + name + "(" + arguments(overload) +
            ");trace={1,unit()};}}");
        for (const bool optimize : {false,true}) for (const u32 seed : {0u,7u,0xffffffffu}) {
            const auto actual = execute(compileProgram(program, {}, optimize), seed);
            const auto expected = execute(effects.rng == RngEffect::AdvanceCurrent ? advancingReference : reference, seed);
            REQUIRE(actual.outputs[0].points.size() == 2);
            for (std::size_t i=0;i<2;++i)
                CHECK(std::bit_cast<u32>(actual.outputs[0].points[i].y) ==
                      std::bit_cast<u32>(expected.outputs[0].points[i].y));
            if (effects.writesCanvas) CHECK(actual.canvas.pixels != expected.canvas.pixels);
            else CHECK(actual.canvas.pixels == expected.canvas.pixels);
        }
    }
}

TEST_CASE("seeded choices and roll restore the parent after eager effectful arguments", "[effects]") {
    const auto program = ips::parse(R"(asset t {size 8x8 output list point trace
        draw {
            trace={0,unit()};
            selected=choose(value1=note(3),seed=note(1),value0=note(2)); trace={selected,unit()};
            rolled=roll(b=note(6),seed=note(4),a=note(5)); trace={rolled,unit()};
            selected=pick(value1=note(8),value0=note(7)); trace={selected,unit()};
        }
        note(tag:i32)->i32 {trace={tag,unit()}; return tag;}
    })");
    const auto reference = compileChecked(R"(asset t {size 8x8 output list point trace
        draw {trace={0,unit()};trace={1,unit()};trace={2,unit()};trace={3,unit()};
              trace={0,unit()};trace={4,unit()};trace={5,unit()};trace={6,unit()};
              trace={0,unit()};trace={7,unit()};trace={8,unit()};index(2);trace={0,unit()};}
    })");
    for (const bool optimize : {false,true}) for (const u32 seed : {0u,7u,0xffffffffu}) {
        const auto actual = execute(compileProgram(program, {}, optimize), seed);
        const auto expected = execute(reference, seed);
        REQUIRE(actual.outputs[0].points.size() == 12);
        for (std::size_t i=0;i<12;++i) {
            if (i != 4 && i != 8 && i != 11)
                CHECK(actual.outputs[0].points[i].x == expected.outputs[0].points[i].x);
            CHECK(std::bit_cast<u32>(actual.outputs[0].points[i].y) ==
                  std::bit_cast<u32>(expected.outputs[0].points[i].y));
        }
        CHECK((actual.outputs[0].points[4].x == 2 || actual.outputs[0].points[4].x == 3));
        CHECK(actual.outputs[0].points[8].x >= 5.f);
        CHECK(actual.outputs[0].points[8].x < 6.f);
        CHECK((actual.outputs[0].points[11].x == 7 || actual.outputs[0].points[11].x == 8));
    }
}

TEST_CASE("stream controls and helper summaries preserve their advertised observable effects", "[effects]") {
    const auto actual = execute(compileChecked(R"(asset t {size 8x8 output list point trace output point last
        draw{trace={0,unit()}; stream_push(7); unit(); stream_pop(); helper(); last={9,9};}
        helper(){trace={1,unit()};last={1,2};pixel(2,3,#abc);}
    })"), 42);
    const auto expected = execute(compileChecked(R"(asset t {size 8x8 output list point trace output point last
        draw{trace={0,unit()};trace={1,unit()};last={9,9};pixel(2,3,#abc);}
    })"), 42);
    REQUIRE(actual.outputs[0].points.size() == 2);
    for (std::size_t i=0;i<2;++i)
        CHECK(std::bit_cast<u32>(actual.outputs[0].points[i].y) ==
              std::bit_cast<u32>(expected.outputs[0].points[i].y));
    REQUIRE(actual.outputs[1].points.size() == 1);
    CHECK(actual.outputs[1].points[0].x == 9);
    CHECK(actual.outputs[1].points[0].y == 9);
    CHECK(actual.canvas.pixels == expected.canvas.pixels);
    CHECK(actual.canvas.at(2,3) == Color{170,187,204,255});
    expectTrap(compileChecked("asset t {size 8x8 draw{stream_pop();}}"), ErrorReason::StackUnderflow);
}

TEST_CASE("every builtin numeric-range effect has an executable accepted-source trap", "[effects]") {
    for (const auto& [name,spec] : ips::builtinSpecs()) for (const auto& overload : spec.overloads) {
        if ((ips::builtinEffects(spec, &overload).checks & static_cast<u8>(EffectCheck::NumericRange)) == 0) continue;
        INFO(name << " arity " << overload.params.size());
        const auto program = ips::parse("asset t {size 8x8 draw{" + name + "(" + arguments(overload,true) + ");}}");
        for (const bool optimize : {false,true})
            expectTrap(compileProgram(program, {}, optimize), ErrorReason::NumericRange);
    }
    for (const bool optimize : {false,true}) {
        const auto polygon = ips::parse("asset t {size 8x8 draw{poly(#fff,1.0/0.0,0,1,0,0,1);}}");
        expectTrap(compileProgram(polygon, {}, optimize), ErrorReason::NumericRange);
    }
}

TEST_CASE("intrinsic trap summaries exclude argument traps and execution-wide resource failures", "[effects]") {
    CHECK(ips::builtinEffects(ips::builtinSpecs().at("abs"),
          &ips::builtinSpecs().at("abs").overloads[0]).checks == 0);
    expectTrap(compileChecked("asset t {size 8x8 draw{abs(1/v);}}"), ErrorReason::DivideByZero);
    expectTrap(compileChecked("asset t {size 8x8 draw{abs(0x80000000/-1);}}"), ErrorReason::IntegerOverflow);
    const auto finiteCast = execute(compileChecked(R"(asset t {size 8x8 output point p
        draw{p={int(1.0/0.0),round(1.0/0.0)};}
    })"));
    CHECK(finiteCast.outputs[0].points[0].x == 2147483648.f);
    CHECK(finiteCast.outputs[0].points[0].y == 2147483648.f);
    auto file = loadFile(compileChecked("asset t {size 8x8 output point p draw{p={abs(float(v)),0};}}"));
    file.meta.fuel = 1;
    VM vm(file, {});
    try { vm.run(); FAIL("fuel limit did not trap"); }
    catch (const Trap& trap) { CHECK(trap.reason == ErrorReason::FuelExhausted); }
    // An acyclic helper chain is valid source, yet still consumes call frames.
    std::string chain = "asset t {size 8x8 draw{f0();}";
    for (unsigned i=0;i<=kMaxCallDepth;++i) {
        chain += "f" + std::to_string(i) + "(){";
        if (i<kMaxCallDepth) chain += "f" + std::to_string(i+1) + "();";
        chain += '}';
    }
    chain += '}';
    expectTrap(compileChecked(chain), ErrorReason::CallDepth);
}

TEST_CASE("part summaries distinguish placement checks missing outputs and isolated RNG", "[effects]") {
    const auto child = ips::parse(R"(asset child {size 4x4 output point p draw{unit();pixel(0,0,#abc);}})");
    for (const auto [body,reason] : {
        std::pair{"child.draw(seed,v,a,at={1.0/0.0,0});", ErrorReason::InvalidPart},
        std::pair{"child.draw(seed,v,a,at={0,0}); p=child.outputs.p;", ErrorReason::PartOutputMissing},
        std::pair{"p=child.outputs.p;", ErrorReason::InvalidOutput}}) {
        const auto root = ips::parse("import child from \"child.ips\" asset t {size 8x8 output point p draw{" +
                                    std::string(body) + "}}");
        for (const bool optimize : {false,true})
            expectTrap(compileProgram(root, {{"child",&child}}, optimize), reason);
    }
    const auto root = ips::parse(R"(import child from "child.ips" asset t {size 8x8 output point p
        draw{child.draw(seed,v,a,at={0,0});p={0,unit()};}})");
    const auto actual = execute(compileProgram(root, {{"child",&child}}));
    const auto expected = execute(compileChecked("asset t {size 8x8 output point p draw{p={0,unit()};}}"));
    CHECK(std::bit_cast<u32>(actual.outputs[0].points[0].y) == std::bit_cast<u32>(expected.outputs[0].points[0].y));
    CHECK(actual.canvas.at(0,0) == Color{170,187,204,255});
}
