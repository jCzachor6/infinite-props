#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>

#include <bit>
#include <map>
#include <string>

#include "Compiler.hpp"
#include "Plan.hpp"
#include "iprop/Debug.hpp"
#include "ips/DeclarationConstants.hpp"
#include "ips/Literals.hpp"
#include "ips/Parser.hpp"
#include "ips/Printer.hpp"
#include "ips/Semantics.hpp"
#include "support/compiler_support.hpp"

using namespace iprop;
using namespace compiler_test;

TEST_CASE("signed input defaults check mathematical range in checker and planner") {
    for (const std::string text : {"-2147483648", "2147483647", "-0x80000000",
                                  "0x7fffffff", "-0", "((-(7)))"}) {
        INFO(text);
        const auto source = "asset t { size 8x8 input i32 n=" + text + " draw{} }";
        const auto program = ips::parse(source);
        CHECK_NOTHROW(ips::checkAsset(program, {}));
        CHECK_NOTHROW(compileProgram(program, {}));
    }
    for (const std::string text : {"2147483648", "-2147483649", "0xffffffff",
                                  "4294967296", "-0xffffffff"}) {
        INFO(text);
        const auto source = "asset t { size 8x8 input i32 n=" + text + " draw{} }";
        const auto program = ips::parse(source);
        CHECK_THROWS_WITH(ips::checkAsset(program, {}),
            "default of input i32 'n' must be in [-2147483648, 2147483647] before wrapping");
        CHECK_THROWS_AS(compileProgram(program, {}), ips::SourceError);
        const auto parent = ips::parse(R"(import child from "child.ips"
            asset parent { size 8x8 draw { child.draw(seed,v,a,at={0,0}) } })");
        CHECK_THROWS_AS(compileProgram(parent, {{"child", &program}}), ips::SourceError);
    }
    for (const std::string text : {"1.0", "true", "#fff"}) {
        const auto program = ips::parse("asset t { size 8x8 input i32 n=" + text + " draw{} }");
        CHECK_THROWS_AS(ips::checkAsset(program, {}), ips::SourceError);
    }
}

TEST_CASE("signed inputs use frozen U32 metadata and host bit pattern overrides") {
    const auto signedProgram = ips::parse(R"(asset integers { size 8x8
        input i32 minimum=-2147483648 input i32 maximum=2147483647 input i32 n=-7
        output point bounds output point value
        draw { bounds={minimum,maximum} value={n,unit()} pixel(0,0,#abc) }
    })");
    const auto unsignedProgram = ips::parse(R"(asset integers { size 8x8
        input u32 minimum=2147483648 input u32 maximum=2147483647 input u32 n=4294967289
        output point bounds output point value
        draw { bounds={minimum,maximum} value={n,unit()} pixel(0,0,#abc) }
    })");
    ips::checkAsset(signedProgram, {});
    ips::checkAsset(unsignedProgram, {});
    const auto formatted = ips::parse(ips::formatProgram(signedProgram));
    for (const bool optimize : {false, true}) {
        const auto bytes = compileProgram(signedProgram, {}, optimize);
        CHECK(stripDebug(bytes) == stripDebug(compileProgram(unsignedProgram, {}, optimize)));
        CHECK(stripDebug(bytes) == stripDebug(compileProgram(formatted, {}, optimize)));
        const auto loaded = loadFile(bytes);
        REQUIRE(loaded.meta.inputs.size() == 3);
        CHECK(loaded.meta.inputs[0].type == iprop::InputType::U32);
        CHECK(loaded.meta.inputs[0].u32Default == 0x80000000u);
        CHECK(loaded.meta.inputs[1].u32Default == 0x7fffffffu);
        CHECK(loaded.meta.inputs[2].u32Default == 0xfffffff9u);
        const auto defaults = execute(bytes);
        CHECK(defaults.outputs[0].points[0].x == -2147483648.f);
        CHECK(defaults.outputs[0].points[0].y == 2147483648.f); // i32 max rounds to f32
        CHECK(defaults.outputs[1].points[0].x == -7.f);
        for (const u32 bits : {0u, 0xffffffffu, 0x80000000u, 0x7fffffffu}) {
            GenerationInputs inputs{42,0,0,0,0,{}};
            inputs.uniforms["n"] = UniformValue{iprop::InputType::U32,{},0.f,bits};
            VM vm(loaded, inputs);
            vm.run();
            const auto result = vm.result();
            CHECK(result.outputs[1].points[0].x == static_cast<f32>(std::bit_cast<i32>(bits)));
            CHECK(std::bit_cast<u32>(result.outputs[1].points[0].y) ==
                  std::bit_cast<u32>(defaults.outputs[1].points[0].y));
        }
        GenerationInputs wrong{42,0,0,0,0,{}};
        wrong.uniforms["n"] = UniformValue{iprop::InputType::F32,{},-1.f,0};
        CHECK_THROWS_AS(VM(loaded, wrong), Trap);
    }
}

TEST_CASE("signed part defaults and overrides retain i32 function parameters") {
    const auto part = ips::parse(R"(asset child { size 8x8 input i32 n=-7
        output point value draw { value={n,unit()} }
    })");
    const auto parent = ips::parse(R"(import first from "child.ips"
        import second from "child.ips" asset parent { size 8x8 input i32 host=-9
        draw { first.draw(seed,v,a,at={0,0}) second.draw(seed,v,a,n=host,at={0,0}) }
    })");
    ips::checkAsset(part, {});
    ips::checkAsset(parent, {{"first",ips::partInfoOf(part.asset)},
                             {"second",ips::partInfoOf(part.asset)}});
    for (const bool optimize : {false,true}) {
        const auto bytes = compileProgram(parent, {{"first",&part},{"second",&part}}, optimize);
        const auto loaded = loadFile(bytes);
        REQUIRE(loaded.code.parts.size() == 2);
        for (const auto& entry : loaded.code.parts) {
            CHECK(entry.inputs[0].type == iprop::InputType::U32);
            CHECK(entry.inputs[0].u32Default == 0xfffffff9u);
        }
        const auto defaults = execute(bytes);
        REQUIRE(defaults.outputs.size() == 2);
        CHECK(defaults.outputs[0].points[0].x == -7.f);
        CHECK(defaults.outputs[1].points[0].x == -9.f);
        GenerationInputs inputs{42,0,0,0,0,{}};
        inputs.uniforms["host"] = UniformValue{iprop::InputType::U32,{},0.f,0xffffffffu};
        VM vm(loaded, inputs);
        vm.run();
        CHECK(vm.result().outputs[0].points[0].x == -7.f);
        CHECK(vm.result().outputs[1].points[0].x == -1.f);
        const auto invalid = ips::parse(R"(import child from "child.ips"
            asset t { size 8x8 draw { child.draw(seed,v,a,n=0.5,at={0,0}) } })");
        CHECK_THROWS_AS(ips::checkAsset(invalid, {{"child",ips::partInfoOf(part.asset)}}), ips::SourceError);
    }
}

TEST_CASE("declaration constants reject operators calls and repeated negation") {
    for (const std::string value : {"1 + 2", "(1 + 2)", "unit()", "seed",
            "--1", "-(-1)", "!1", "~1", "-true", "-#fff"}) {
        INFO(value);
        const auto checkDefault = [&]() {
            const auto program = ips::parse("asset t { size 8x8 input f32 x = " +
                value + " draw {} }");
            ips::checkAsset(program, {});
            return compileProgram(program, {});
        };
        const auto checkPalette = [&]() {
            const auto program = ips::parse("asset t { size 8x8 palette p = [" +
                value + "] draw {} }");
            ips::checkAsset(program, {});
            return compileProgram(program, {});
        };
        CHECK_THROWS_AS(checkDefault(), ips::SourceError);
        CHECK_THROWS_AS(checkPalette(), ips::SourceError);
    }
    const auto program = ips::parse("asset t { size 8x8 input u32 bits = -(0xffffffff) draw {} }");
    try {
        ips::checkAsset(program, {});
        FAIL("a mathematically negative U32 default must be rejected before wrapping");
    } catch (const ips::SourceError& error) {
        CHECK(error.message.find("negative default") != std::string::npos);
        CHECK(error.message.find("input i32") != std::string::npos);
    }
    CHECK_THROWS_AS(compiler::Planner(program, {}).plan(), ips::SourceError);
}

TEST_CASE("resolved constants retain mathematical integers without changing wrapped values") {
    struct Case { const char* text; i64 mathematical; i32 wrapped; };
    const Case cases[] = {
        {"0xffffffff", 4294967295LL, -1},
        {"-0xffffffff", -4294967295LL, 1},
        {"-2147483648", -2147483648LL, -2147483647 - 1},
        {"4294967296", 4294967296LL, 0},
    };
    for (const auto& test : cases) {
        const auto program = ips::parse(std::string("asset t { size 8x8 palette p = [") +
            test.text + "] draw {} }");
        const auto value = ips::resolveDeclarationConstant(*program.asset.palettes[0].elements[0]);
        CHECK(value.mathematicalInteger == test.mathematical);
        CHECK(std::get<i32>(value.value) == test.wrapped);
    }
    const auto source = "asset t { size 8x8 output point p draw { p = {-0xffffffff, 4294967296} } }";
    for (const bool optimize : {false, true}) {
        const auto result = execute(compileProgram(ips::parse(source), {}, optimize));
        CHECK(result.outputs[0].points[0].x == 1.f);
        CHECK(result.outputs[0].points[0].y == 0.f);
    }
}

TEST_CASE("signed root defaults preserve bits rendering and canonical roundtrips") {
    const auto program = ips::parse(R"(asset defaults { size 8x8
        input f32 amount = -(0.5)
        input f32 zero = (-0.0)
        input f32 integer = ((-2))
        input u32 bits = (0xffffffff)
        input color tint = (#39f)
        output point numbers
        output point patterns
        draw { numbers = {amount, integer} patterns = {bits, zero} pixel(1,1,tint) }
    })");
    ips::checkAsset(program, {});
    const auto formatted = ips::parse(ips::formatProgram(program));
    for (const bool optimize : {false, true}) {
        const auto bytes = compileProgram(program, {}, optimize);
        CHECK(stripDebug(bytes) == stripDebug(compileProgram(formatted, {}, optimize)));
        const auto loaded = loadFile(bytes);
        CHECK(loaded.meta.inputs[0].f32Default == -0.5f);
        CHECK(std::bit_cast<u32>(loaded.meta.inputs[1].f32Default) == 0x80000000u);
        CHECK(loaded.meta.inputs[2].f32Default == -2.f);
        CHECK(loaded.meta.inputs[3].u32Default == 0xffffffffu);
        const auto result = execute(bytes);
        CHECK(result.outputs[0].points[0].x == -0.5f);
        CHECK(result.outputs[0].points[0].y == -2.f);
        CHECK(result.outputs[1].points[0].x == -1.f);
        CHECK(std::bit_cast<u32>(result.outputs[1].points[0].y) == 0x80000000u);
        CHECK(result.canvas.at(1,1) == Color{0x33,0x99,0xff,0xff});
    }
}

TEST_CASE("signed part defaults use the same resolved values as root metadata") {
    const auto part = ips::parse(R"(asset child { size 8x8
        input f32 amount = -(0.5)
        input f32 zero = -(0.0)
        input f32 integer = (-2)
        input u32 bits = 0xffffffff
        input color tint = (#39f)
        output point numbers
        output point patterns
        draw { numbers={amount,integer} patterns={bits,zero} pixel(1,1,tint) }
    })");
    const auto root = ips::parse(R"(import first from "child.ips"
        import second from "child.ips"
        asset parent { size 8x8 draw {
          first.draw(seed,v,a,at={0,0})
          second.draw(seed,v,a,amount=-0.75,integer=-3,tint=#f00,at={0,0})
        } }
    )");
    ips::checkAsset(part, {});
    ips::checkAsset(root, {{"first", ips::partInfoOf(part.asset)},
                          {"second", ips::partInfoOf(part.asset)}});
    const auto standalone = execute(compileChecked(R"(asset child { size 8x8
        input f32 amount = -(0.5) input f32 zero = -(0.0)
        input f32 integer = (-2) input u32 bits = 0xffffffff input color tint = (#39f)
        output point numbers output point patterns
        draw { numbers={amount,integer} patterns={bits,zero} pixel(1,1,tint) }
    })"));
    for (const bool optimize : {false, true}) {
        const auto bytes = compileProgram(root, {{"first", &part}, {"second", &part}}, optimize);
        const auto loaded = loadFile(bytes);
        REQUIRE(loaded.code.parts.size() == 2);
        for (const auto& entry : loaded.code.parts) {
            CHECK(entry.inputs[0].f32Default == -0.5f);
            CHECK(std::bit_cast<u32>(entry.inputs[1].f32Default) == 0x80000000u);
            CHECK(entry.inputs[2].f32Default == -2.f);
            CHECK(entry.inputs[3].u32Default == 0xffffffffu);
        }
        const auto result = execute(bytes);
        REQUIRE(result.outputs.size() == 4);
        CHECK(result.outputs[0].points[0].x == standalone.outputs[0].points[0].x);
        CHECK(result.outputs[0].points[0].y == standalone.outputs[0].points[0].y);
        CHECK(result.outputs[1].points[0].x == -1.f);
        CHECK(result.outputs[2].points[0].x == -0.75f);
        CHECK(result.outputs[2].points[0].y == -3.f);
        CHECK(result.canvas.at(1,1) == Color{255,0,0,255});
    }
}

TEST_CASE("integer-spelled f32 defaults preserve released conversions in roots and parts") {
    for (const std::string literal : {"2", "010", "0xffffffff", "4294967296", "9223372036854775808"}) {
        INFO(literal);
        u32 expectedBits = 0;
        REQUIRE(ips::parseFloatBits(literal, expectedBits));
        const auto part = ips::parse("asset child { size 8x8 input f32 value = " + literal +
            " output point p draw { p = {value,0} } }");
        const auto root = ips::parse("import child from \"child.ips\" asset root { size 8x8 "
            "input f32 value = " + literal + " output point p draw { p={value,0} child.draw(seed,v,a,at={0,0}) } }");
        ips::checkAsset(part, {});
        ips::checkAsset(root, {{"child", ips::partInfoOf(part.asset)}});
        for (const bool optimize : {false, true}) {
            const auto bytes = compileProgram(root, {{"child", &part}}, optimize);
            const auto loaded = loadFile(bytes);
            CHECK(std::bit_cast<u32>(loaded.meta.inputs[0].f32Default) == expectedBits);
            CHECK(std::bit_cast<u32>(loaded.code.parts[0].inputs[0].f32Default) == expectedBits);
            const auto result = execute(bytes);
            REQUIRE(result.outputs.size() == 2);
            CHECK(std::bit_cast<u32>(result.outputs[0].points[0].x) == expectedBits);
            CHECK(std::bit_cast<u32>(result.outputs[1].points[0].x) == expectedBits);
        }
    }
}

TEST_CASE("signed palettes preserve integer wrapping negative zero and dynamic selection") {
    const auto program = ips::parse(R"(asset palettes { size 8x8 variants 4
        palette integers = [-(2), -1, -0xffffffff]
        palette floats = [-0.5, (-0.0), 1.0]
        palette flags = [true, false]
        output point values output point flag
        draw {
          values = {integers[v],floats[v]}
          flag = {flags[v],0}
          pixel(2 + integers[v],0,#f00)
        }
    })");
    ips::checkAsset(program, {});
    for (const bool optimize : {false, true}) {
        const auto bytes = compileProgram(program, {}, optimize);
        for (u32 variant = 0; variant < 4; ++variant) {
            const auto result = run(bytes, variant, 0);
            const i32 integers[] = {-2,-1,1};
            const f32 floats[] = {-0.5f,-0.f,1.f};
            const auto index = variant < 3 ? variant : 2;
            CHECK(result.outputs[0].points[0].x == integers[index]);
            CHECK(std::bit_cast<u32>(result.outputs[0].points[0].y) == std::bit_cast<u32>(floats[index]));
            CHECK(result.outputs[1].points[0].x == (variant == 0 ? 1.f : 0.f));
            CHECK(result.canvas.at(static_cast<u32>(2 + integers[index]),0) == Color{255,0,0,255});
        }
    }
    const auto mixed = ips::parse("asset t { size 8x8 palette p=[-1,0.5] draw {} }");
    CHECK_NOTHROW(ips::checkAsset(mixed, {}));
}
