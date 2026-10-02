#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <bit>

#include "Compiler.hpp"
#include "iprop/Debug.hpp"
#include "iprop/Instruction.hpp"
#include "ips/Parser.hpp"
#include "ips/Semantics.hpp"
#include "support/compiler_support.hpp"

using namespace iprop;

namespace {
auto compile(const std::string& source, bool optimize) {
    auto program = ips::parse(source);
    ips::checkAsset(program, {});
    return compileProgram(program, {}, optimize);
}
void compare(const std::vector<u8>& actual, const std::vector<u8>& expected, u32 variants = 1) {
    CHECK(actual.size() <= expected.size());
    CHECK(stripDebug(actual).size() <= stripDebug(expected).size());
    for (const u32 seed : {0u, 7u, 26u, 0xffffffffu}) {
        for (u32 variant = 0; variant < variants; ++variant) {
        const auto execute = [&](const std::vector<u8>& bytes) {
            const auto loaded = loadFile(bytes);
            validate(loaded);
            VM vm(loaded, GenerationInputs{seed, variant, 0, 0, 0, {}});
            vm.run();
            return vm.result();
        };
        const auto a = execute(actual), b = execute(expected);
        CHECK(a.image.pixels == b.image.pixels);
        REQUIRE(a.outputs.size() == b.outputs.size());
        for (std::size_t i = 0; i < a.outputs.size(); ++i) {
            CHECK(a.outputs[i].name == b.outputs[i].name);
            REQUIRE(a.outputs[i].points.size() == b.outputs[i].points.size());
            for (std::size_t j = 0; j < a.outputs[i].points.size(); ++j) {
                CHECK(std::bit_cast<u32>(a.outputs[i].points[j].x) == std::bit_cast<u32>(b.outputs[i].points[j].x));
                CHECK(std::bit_cast<u32>(a.outputs[i].points[j].y) == std::bit_cast<u32>(b.outputs[i].points[j].y));
            }
        }
        }
    }
}
}

TEST_CASE("DCE removes dead stores locals names and unused palette constants") {
    const std::string source = R"(asset t { size 8x8
      palette p=[#123456,#abcdef,#334455]
      palette unused=[#998877,#665544]
      draw { dead=1234567 c=p[0] c=p[1] pixel(0,0,c) } })";
    const auto expected = compile(source, false), actual = compile(source, true);
    compare(actual, expected);
    const auto file = loadFile(actual);
    CHECK(file.code.entry().locals.size() <= 1);
    CHECK(file.pool.colors == std::vector<Color>{{0xab,0xcd,0xef,0xff}});
    CHECK(std::find(file.pool.strings.begin(), file.pool.strings.end(), "dead") == file.pool.strings.end());
    CHECK(actual.size() < expected.size());
    CHECK(actual == compile(source, true));
}

TEST_CASE("DCE retains RNG calls helper effects loop carries and branch liveness") {
    const std::string source = R"(asset t { size 8x8 variants 2 output point result
      draw {
        dead=index(3) unused=effect() i=0 x=17
        while(i<3) { x=x+i i=i+1 }
        if (v==0) { x=x+1 } else { x=99 }
        result={x,unit()}
        for j in 0..3 { pixel(j,0,#fff) }
      }
      effect()->i32 { pixel(4,0,#f00) return index(7) }
    })";
    compare(compile(source, true), compile(source, false), 2);
    for (bool optimize : {false,true}) {
        const auto program = compile("asset t { size 8x8 draw { dead = 1 / v } }", optimize);
        CHECK_THROWS_AS(compiler_test::execute(program), Trap);
    }
}

TEST_CASE("constant chains color and vector kernels fold with exact result bits") {
    const std::string source = R"(asset t { size 8x8 output point result output point nonfinite
      draw {
        x=6 y=x/2 z=y*4
        c=color.hsv(color.mix(color.shade(#aabbccdd,-25),#11334455,0.25),0.125,0.75,1.1)
        pixel(0,0,color.mul(c,rgba(255,128,z,255)))
        q=vec2.rot({3,-0.0},0.0)
        result={vec2.len(q),vec2.dot(q,{1,2})}
        nonfinite={1.0,0.0}/{0.0,0.0}
      } })";
    const auto actual = compile(source, true), reference = compile(source, false);
    compare(actual, reference);
    CHECK(loadFile(actual).code.entry().code.size() < loadFile(reference).code.entry().code.size());
    const auto loaded = loadFile(actual);
    const auto debug = parseDebug(actual, *loaded.layout.find(SectionId::Debug));
    CHECK(debug.functions[0].lines.size() == loaded.code.entry().code.size() / 4);
    CHECK(debug.functions[0].localNames.size() == loaded.code.entry().params.size() + loaded.code.entry().locals.size());
    for (const auto name : debug.functions[0].localNames)
        if (name != kAnonymousName) CHECK(name < loaded.pool.strings.size());
}

TEST_CASE("folding preserves dividing overflow HSV traps and mutable control regions") {
    for (const auto source : {
        "asset t { size 8x8 draw { dead=1/0 } }",
        "asset t { size 8x8 draw { dead=0x80000000/-1 } }",
        "asset t { size 8x8 draw { dead=color.hsv(#fff,1.0/0.0,1,1) } }"}) {
        for (bool optimize : {false,true}) CHECK_THROWS_AS(compiler_test::execute(compile(source,optimize)), Trap);
    }
    const std::string source = R"(asset t { size 8x8 variants 2 output point p
      draw { x=3 if(v==0) { x=4 } else { x=5 } i=0 while(i<3) { x=x+i i=i+1 } p={x,min(-0.0,0.0)} } })";
    compare(compile(source,true),compile(source,false),2);
}

TEST_CASE("CSE reuses stored pure values with versioned mutable locals") {
    const std::string repeated = R"(asset t { size 8x8 variants 2 output point p
      draw { x=(v+17)*(a+3) y=(v+17)*(a+3) p={x,y} } })";
    const auto actual = compile(repeated,true), reference = compile(repeated,false);
    compare(actual,reference,2);
    CHECK(loadFile(actual).code.entry().code.size() < loadFile(reference).code.entry().code.size());
    const auto loaded = loadFile(actual);
    unsigned products = 0;
    for (std::size_t i = 0; i < loaded.code.entry().code.size(); i += 4)
        if (decodeInstruction(loaded.code.entry().code.subspan(i, 4)).opcode == Opcode::I32Mul) ++products;
    CHECK(products == 1);
    const std::string mutableSource = R"(asset t { size 8x8 variants 2 output point p
      draw { x=v+1 y=x*3 x=x+1 z=x*3 p={y,z} } })";
    compare(compile(mutableSource,true),compile(mutableSource,false),2);
    const std::string effects = R"(asset t { size 8x8 variants 2 output point p
      draw { x=unit()+1 y=unit()+1 p={x,y} } })";
    compare(compile(effects,true),compile(effects,false),2);
}

TEST_CASE("small constant loops unroll with final counters and side effects preserved") {
    const std::string source = R"(asset t { size 8x8 output point p
      draw { for i in 0..3 { pixel(i,0,color.shade(#f00,pick(20))) } for j in 5..2 { pixel(0,1,#fff) } p={i,j} } })";
    const auto actual = compile(source,true), reference = compile(source,false);
    compare(actual,reference);
    const auto loaded = loadFile(actual);
    unsigned loops = 0;
    for (std::size_t i = 0; i < loaded.code.entry().code.size(); i += 4)
        if (decodeInstruction(loaded.code.entry().code.subspan(i,4)).opcode == Opcode::Loop) ++loops;
    CHECK(loops == 0);
    CHECK(compiler_test::execute(actual).outputs[0].points[0].x == 3);
    CHECK(compiler_test::execute(actual).outputs[0].points[0].y == 5);
    for (const auto body : {
        "for i in 0..3 { if(i==1) { continue } pixel(i,0,#fff) }",
        "for i in 0..3 { if(i==1) { break } pixel(i,0,#fff) }",
        "for i in 0..5 { pixel(i,0,#fff) i=i+1 }",
        "for i in 0..3 { for j in 0..3 { pixel(i,j,#fff) } }"}) {
        const std::string nested = "asset t { size 8x8 draw { " + std::string(body) + " } }";
        compare(compile(nested,true),compile(nested,false));
    }
}

TEST_CASE("strength reduction preserves signed arithmetic and wrapping bits") {
    const std::string source = R"(asset t { size 8x8 variants 2 input u32 bits=0x80000001 output point p output point q
      draw { x=bits*2 p={x,bits/2} q={(bits&0x7fffffff)/2,((bits>>1)/2)} } })";
    const auto actual = compile(source,true), reference = compile(source,false);
    compare(actual,reference,2);
    const auto loaded = loadFile(actual);
    unsigned adds=0, divisions=0, shifts=0;
    for (std::size_t i=0;i<loaded.code.entry().code.size();i+=4) {
        const auto op=decodeInstruction(loaded.code.entry().code.subspan(i,4)).opcode;
        if(op==Opcode::I32Add) ++adds;
        if(op==Opcode::I32DivS) ++divisions;
        if(op==Opcode::I32ShrU) ++shifts;
    }
    CHECK(adds >= 1);
    CHECK(divisions == 1); // negative-capable input retains truncation
    CHECK(shifts == 3);
    const auto result=compiler_test::execute(actual);
    CHECK(result.outputs[0].points[0].x == 2);
    CHECK(result.outputs[0].points[0].y == -1073741823.0f);
}
