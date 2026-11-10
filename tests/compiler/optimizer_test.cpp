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
            VM vm(loaded, GenerationInputs{seed, 0, 0, {}});
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
    const std::string source = R"(asset t { input i32 v = 0  size 8x8 output point result
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
        const auto program = compile("asset t { input i32 v = 0  size 8x8 draw { dead = 1 / v } }", optimize);
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
    const std::string source = R"(asset t { input i32 v = 0  size 8x8 output point p
      draw { x=3 if(v==0) { x=4 } else { x=5 } i=0 while(i<3) { x=x+i i=i+1 } p={x,min(-0.0,0.0)} } })";
    compare(compile(source,true),compile(source,false),2);
}

TEST_CASE("CSE reuses stored pure values with versioned mutable locals") {
    const std::string repeated = R"(asset t { input i32 a = 0 input i32 v = 0  size 8x8 output point p
      draw { x=(v+17)*(a+3) y=(v+17)*(a+3) p={x,y} } })";
    const auto actual = compile(repeated,true), reference = compile(repeated,false);
    compare(actual,reference,2);
    CHECK(loadFile(actual).code.entry().code.size() < loadFile(reference).code.entry().code.size());
    const auto loaded = loadFile(actual);
    unsigned products = 0;
    for (std::size_t i = 0; i < loaded.code.entry().code.size(); i += 4)
        if (decodeInstruction(loaded.code.entry().code.subspan(i, 4)).opcode == Opcode::I32Mul) ++products;
    CHECK(products == 1);
    const std::string mutableSource = R"(asset t { input i32 v = 0  size 8x8 output point p
      draw { x=v+1 y=x*3 x=x+1 z=x*3 p={y,z} } })";
    compare(compile(mutableSource,true),compile(mutableSource,false),2);
    const std::string effects = R"(asset t { size 8x8 output point p
      draw { x=unit()+1 y=unit()+1 p={x,y} } })";
    compare(compile(effects,true),compile(effects,false),2);
}

TEST_CASE("small constant loops unroll with final counters and side effects preserved") {
    const std::string source = R"(asset t { size 8x8 output point p
      draw { for i in 0..3 { pixel(i,0,color.shade(#f00,index(20))) } for j in 5..2 { pixel(0,1,#fff) } p={i,j} } })";
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
        const std::string nested = "asset t { input i32 v=0 size 8x8 draw { " + std::string(body) + " } }";
        compare(compile(nested,true),compile(nested,false));
    }
}

TEST_CASE("strength reduction preserves signed arithmetic and wrapping bits") {
    const std::string source = R"(asset t { size 8x8 input u32 bits=0x80000001 output point p output point q
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

TEST_CASE("inline repeated expressions use one computation and anonymous memo storage") {
    const std::string source = R"(asset t { input i32 a = 0 input i32 v = 0  size 8x8 output point p
      draw { p={(v+17)*(a+3),(v+17)*(a+3)} } })";
    const auto actual = compile(source,true), reference = compile(source,false);
    compare(actual,reference,2);
    const auto loaded = loadFile(actual);
    unsigned products = 0;
    for (std::size_t i = 0; i < loaded.code.entry().code.size(); i += 4)
        if (decodeInstruction(loaded.code.entry().code.subspan(i,4)).opcode == Opcode::I32Mul) ++products;
    CHECK(products == 1);
    for (const auto body : {
        "p={(v+17)*(a+3),(v+17)*(a+3)} x=unit() p={(v+17)*(a+3)+x,(v+17)*(a+3)}",
        "x=v+1 p={x*3,x*3} x=x+1 p={x*3,x*3}",
        "for i in 0..3 { p={(i+17)*(v+3),(i+17)*(v+3)} }",
        "p={pow(float(v)+1,2.0),pow(float(v)+1,2.0)}"}) {
        const std::string program = "asset t { input i32 v=0 input i32 a=0 size 8x8 output point p draw { " + std::string(body) + " } }";
        compare(compile(program,true),compile(program,false),2);
    }
}

namespace {
std::vector<Opcode> entryOpcodes(const std::vector<u8>& bytes) {
    const auto loaded = loadFile(bytes);
    std::vector<Opcode> opcodes;
    for (std::size_t i = 0; i < loaded.code.entry().code.size(); i += 4)
        opcodes.push_back(decodeInstruction(loaded.code.entry().code.subspan(i,4)).opcode);
    return opcodes;
}
u64 fuelUsed(const std::vector<u8>& bytes) {
    const auto loaded = loadFile(bytes);
    validate(loaded);
    VM vm(loaded, GenerationInputs{7, 0, 0, {}});
    vm.run();
    return vm.fuelBudget() - vm.fuel();
}
}

TEST_CASE("a negated integer comparison becomes the opposite comparison") {
    // Loop exits, `!` and logical operands all test a comparison against 0.
    const std::string source = R"(asset t { size 16x16 input i32 limit=11 output point p
      draw { n=0 while (n < limit) { n=n+1 }
             for y in 0..limit { if (!(y > 6)) { pixel(y,0,#f00) } if (!(y == 3) && y != 9) { pixel(y,1,#0f0) } }
             u=limit+0 if (!(u <= 10)) { pixel(0,2,#00f) } if (!(!(u >= 11))) { pixel(1,2,#00f) }
             p={n,unit()} } })";
    const auto actual = compile(source,true), reference = compile(source,false);
    compare(actual,reference);
    const auto opcodes = entryOpcodes(actual);
    for (std::size_t i = 2; i < opcodes.size(); ++i) {
        const bool comparison = opcodes[i - 2] >= Opcode::I32Eq && opcodes[i - 2] <= Opcode::I32GeU;
        CHECK_FALSE((comparison && opcodes[i - 1] == Opcode::ConstI32 &&
                     (opcodes[i] == Opcode::I32Eq || opcodes[i] == Opcode::I32Ne)));
    }
    CHECK(std::count(opcodes.begin(), opcodes.end(), Opcode::I32GeS) >= 2); // both loop exits
    CHECK(fuelUsed(actual) < fuelUsed(reference));
    CHECK(compiler_test::execute(actual).outputs[0].points[0].x == 11);
}

TEST_CASE("an ordered float comparison keeps its negation, which is true for NaN") {
    // nan is 0/0 at run time: every ordered comparison with it is false, so
    // each negation below holds and none may turn into the opposite test.
    const std::string source = R"(asset t { size 8x8 input f32 zero=0.0 output point p
      draw { nan=zero/zero count=0
             if (!(nan < 1.0)) { count=count+1 } if (!(nan > 1.0)) { count=count+1 }
             if (!(nan <= 1.0)) { count=count+1 } if (!(nan >= 1.0)) { count=count+1 }
             if (!(nan == nan)) { count=count+1 } if (nan != nan) { count=count+1 }
             if (!(nan != nan)) { count=count+100 }
             x=zero+2.0 if (!(x < 1.0)) { count=count+1 } if (!(x == 2.0)) { count=count+100 }
             p={count,unit()} } })";
    const auto actual = compile(source,true), reference = compile(source,false);
    compare(actual,reference);
    CHECK(compiler_test::execute(actual).outputs[0].points[0].x == 7);
    CHECK(compiler_test::execute(reference).outputs[0].points[0].x == 7);
    const auto opcodes = entryOpcodes(actual);
    CHECK(std::count(opcodes.begin(), opcodes.end(), Opcode::F32Lt) >= 2);
    CHECK(std::count(opcodes.begin(), opcodes.end(), Opcode::F32Ge) == 1); // only the source's own `>=`
}

TEST_CASE("a for body without continue loses its label and its branches still land") {
    const auto blocks = [](const std::vector<u8>& bytes) {
        const auto opcodes = entryOpcodes(bytes);
        return std::count(opcodes.begin(), opcodes.end(), Opcode::Block);
    };
    // No continue: only the two break labels remain; the nested break crosses
    // the removed label of its own loop and an `if`.
    const std::string plain = R"(asset t { size 16x16 input i32 n=12 output point p
      draw { hits=0 for y in 0..n { for x in 0..n { if (x == y) { pixel(x,y,#fff) hits=hits+1 }
                                                   if (x > 8) { if (y > 2) { break } } } }
             p={hits,unit()} } })";
    const auto actual = compile(plain,true), reference = compile(plain,false);
    compare(actual,reference);
    CHECK(blocks(reference) == 4);
    CHECK(blocks(actual) == 2);
    CHECK(fuelUsed(actual) < fuelUsed(reference));
    // Rows 10 and 11 break at x = 9, before their diagonal pixel.
    CHECK(compiler_test::execute(actual).outputs[0].points[0].x == 10);
    CHECK(compiler_test::execute(reference).outputs[0].points[0].x == 10);

    // A continue keeps its own label, also when an outer loop loses one, and
    // when the continue sits inside a capture or a nested loop.
    for (const auto body : {
        "for i in 0..n { if (i % 2 == 0) { continue } pixel(i,0,#f00) hits=hits+1 }",
        "for y in 0..n { for x in 0..n { if (x < y) { continue } if (x > y + 3) { break } hits=hits+1 } }",
        "for y in 0..n { for x in 0..n { hits=hits+1 } if (y % 3 == 0) { continue } hits=hits+10 }",
        "for y in 0..n { k=0 while (k < y) { k=k+1 if (k == 4) { break } if (k % 2 == 0) { continue } hits=hits+1 } }",
        "layer image(alpha=surface.source_over) { for i in 0..n { if (i == 5) { break } pixel(i,i,#abc) hits=hits+1 } } surface.paint(image)",
        "for y in 0..n { if (y == 7) { break } } for z in 0..n { hits=hits+z }"}) {
        const std::string program = "asset t { size 16x16 input i32 n=12 output point p draw { hits=0 " +
            std::string(body) + " p={hits,unit()} } }";
        CAPTURE(body);
        compare(compile(program,true),compile(program,false));
    }
}
