#include <catch2/catch_test_macros.hpp>
#include "Compiler.hpp"
#include "iprop/VM.hpp"
#include "ips/Parser.hpp"
#include "ips/Semantics.hpp"
#include "support/f32_corpus.hpp"

#include <bit>
#include <cmath>
#include <limits>

using namespace iprop;

namespace {
std::vector<u8> compile(const std::string& source) {
    const auto program = ips::parse(source);
    ips::checkAsset(program, {});
    return compileProgram(program, {});
}
std::string palette(std::size_t size) {
    std::string result = "palette levels = [";
    for (std::size_t i = 0; i < size; ++i) {
        if (i) result += ",";
        result += std::to_string(static_cast<f32>(i*i) * 0.125f);
    }
    return result + "] ";
}
GenerationInputs at(f32 value) {
    GenerationInputs inputs;
    inputs.uniforms["t"] = {InputType::F32, {}, value, 0};
    return inputs;
}
}

TEST_CASE("balanced ramp selection matches an explicit linear reference at every stop and float boundary") {
    for (const std::size_t count : {1,8,32,64,128}) {
        std::string reference = "reference(t:f32) -> f32 { ";
        if (count > 1) {
            reference += "position = min(max(t,0),1) * " + std::to_string(count-1) +
                " i = i32trunc(position) f = position - i ";
            for (std::size_t k = 0; k + 1 < count; ++k) {
                reference += "if (i == " + std::to_string(k) + ") { return lerp(levels[" +
                    std::to_string(k) + "],levels[" + std::to_string(k+1) + "],f) } ";
            }
        }
        reference += "return levels[" + std::to_string(count-1) + "] }";
        const auto bytes = compile("asset t { size 1x1 input f32 t=0.0 output point p " + palette(count) +
            "draw(v:i32,a:i32) { p = {ramp(levels,t), reference(t)} } " + reference + " }");
        const auto file = loadFile(bytes);
        auto values = test_support::f32BoundaryCorpus();
        if (count > 1) for (std::size_t k = 0; k < count; ++k) {
            const auto stop = static_cast<f32>(k) / static_cast<f32>(count-1);
            values.push_back(stop);
            values.push_back(std::nextafter(stop, -std::numeric_limits<f32>::infinity()));
            values.push_back(std::nextafter(stop, std::numeric_limits<f32>::infinity()));
        }
        for (const auto t : values) {
            INFO("palette " << count << " t bits " << std::bit_cast<u32>(t));
            VM vm(file,at(t));
            vm.run();
            const auto point = vm.result().outputs[0].points[0];
            CHECK(std::bit_cast<u32>(point.x) == std::bit_cast<u32>(point.y));
        }
    }
}

TEST_CASE("folded ramp constants preserve dynamic color and float interpolation bits") {
    const std::vector<std::pair<std::string,f32>> values{
        {"0.0",0.f}, {"-0.0",-0.f}, {"0.125",0.125f}, {"0.49999997",0.49999997f},
        {"0.5",0.5f}, {"0.99",0.99f}, {"1.0",1.f}, {"2",2.f}, {"-1",-1.f},
        {"(0.25+0.25)*2",1.f}, {"1.0/0.0",std::numeric_limits<f32>::infinity()},
        {"-1.0/0.0",-std::numeric_limits<f32>::infinity()},
        {"0.0/0.0",std::numeric_limits<f32>::quiet_NaN()}
    };
    for (const std::size_t count : {1,8,32,64,128}) {
        std::string colors = "palette colors = [";
        for (std::size_t k=0;k<count;++k) {
            if (k) colors += ",";
            colors += k % 2 ? "#9e65474d" : "#d0d1c7ff";
        }
        colors += "] ";
        const auto source = [&](const std::string& t) {
            return "asset t { size 1x1 input f32 t=0.0 output point p " + palette(count) + colors +
                "draw(v:i32,a:i32) { p={ramp(levels," + t + "),0} pixel(0,0,ramp(colors," + t + ")) } }";
        };
        const auto dynamicBytes = compile(source("t"));
        const auto dynamic = loadFile(dynamicBytes);
        for (const auto& [expression,value] : values) {
            INFO("palette " << count << " expression " << expression);
            const auto foldedBytes = compile(source(expression));
            const auto folded = loadFile(foldedBytes);
            VM vm(folded,{}), reference(dynamic,at(value));
            vm.run(); reference.run();
            CHECK(vm.canvas().pixels == reference.canvas().pixels);
            CHECK(std::bit_cast<u32>(vm.result().outputs[0].points[0].x) ==
                  std::bit_cast<u32>(reference.result().outputs[0].points[0].x));
            CHECK(folded.code.functions[0].code.size() < 64); // no runtime selection
        }
    }
}

TEST_CASE("large per-pixel ramps stay within a logarithmic fuel bound") {
    u64 constantFuel = 0;
    for (const std::size_t count : {1,8,32,64,128}) {
        for (const bool dynamic : {false,true}) {
            const auto bytes = compile("asset t { size 64x64 input f32 t=0.99 " + palette(count) +
                "draw(v:i32,a:i32) { for y in 0..64 { for x in 0..64 { "
                "value = ramp(levels," + (dynamic ? std::string("t") : "0.99") + ") } } } }");
            const auto file = loadFile(bytes);
            VM vm(file,{});
            REQUIRE_NOTHROW(vm.run());
            const u64 spent = vm.fuelBudget() - vm.fuel();
            if (dynamic) CHECK(spent < 330000);
            else {
                if (!constantFuel) constantFuel = spent;
                CHECK(spent == constantFuel);
            }
        }
    }
}

TEST_CASE("balanced palette indexing preserves negative and out-of-range defaults") {
    for (const std::size_t count : {1,8,32,64,128}) {
        const auto bytes = compile("asset t { size 1x1 output point p " + palette(count) +
            "draw(v:i32,a:i32) { p = {levels[seed],0} } }");
        const auto file = loadFile(bytes);
        std::vector<u32> indices{0x80000000u,0xffffffffu,0x7fffffffu};
        for (u32 i = 0; i <= count; ++i) indices.push_back(i);
        for (const auto index : indices) {
            GenerationInputs inputs; inputs.seed = index;
            VM vm(file,inputs); vm.run();
            const auto chosen = index < count ? index : count-1;
            CHECK(vm.result().outputs[0].points[0].x == static_cast<f32>(chosen*chosen)*0.125f);
        }
    }
}

TEST_CASE("large dynamic ramps retain helper drawing RNG and traps exactly once") {
    for (const std::size_t count : {1,8,128}) {
        const auto declarations = "asset t { size 1x1 output point result output list point trace " +
            palette(count) + "sample() -> f32 { trace={1,unit()} pixel(0,0,#ffffff80) return 0.75 } ";
        const auto bytes = compile(declarations +
            "draw(v:i32,a:i32) { result={ramp(levels,sample()),unit()} } }");
        const auto referenceBytes = compile(declarations +
            "draw(v:i32,a:i32) { sample() result={0,unit()} } }");
        const auto file = loadFile(bytes), reference = loadFile(referenceBytes);
        VM vm(file,{}), expected(reference,{});
        vm.run(); expected.run();
        REQUIRE(vm.result().outputs[1].points.size() == 1);
        CHECK(vm.canvas().at(0,0).r == 128);
        CHECK(std::bit_cast<u32>(vm.result().outputs[0].points[0].y) ==
              std::bit_cast<u32>(expected.result().outputs[0].points[0].y));
        const auto trapBytes = compile("asset t { size 1x1 " + palette(count) +
            "draw(v:i32,a:i32) { ramp(levels,1/v) } }");
        const auto trapFile = loadFile(trapBytes);
        VM trapped(trapFile,{});
        CHECK_THROWS_AS(trapped.run(), Trap);
    }
}
