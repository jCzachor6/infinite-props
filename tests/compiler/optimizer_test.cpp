#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <bit>

#include "Compiler.hpp"
#include "iprop/Debug.hpp"
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
void compare(const std::vector<u8>& actual, const std::vector<u8>& expected) {
    CHECK(actual.size() <= expected.size());
    CHECK(stripDebug(actual).size() <= stripDebug(expected).size());
    for (const u32 seed : {0u, 7u, 26u, 0xffffffffu}) {
        const auto a = compiler_test::execute(actual, seed);
        const auto b = compiler_test::execute(expected, seed);
        CHECK(a.canvas.pixels == b.canvas.pixels);
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

TEST_CASE("DCE removes dead stores locals names and unused palette constants") {
    const std::string source = R"(asset t { size 8x8
      palette p=[#123456,#abcdef,#334455]
      palette unused=[#998877,#665544]
      draw { dead=1234567 c=p[0] c=p[1] pixel(0,0,c) } })";
    const auto expected = compile(source, false), actual = compile(source, true);
    compare(actual, expected);
    const auto file = loadFile(actual);
    CHECK(file.code.entry().locals.size() == 1);
    CHECK(file.pool.colors == std::vector<Color>{{0xab,0xcd,0xef,0xff}});
    CHECK(std::find(file.pool.strings.begin(), file.pool.strings.end(), "dead") == file.pool.strings.end());
    CHECK(actual.size() < expected.size());
    CHECK(actual == compile(source, true));
}

TEST_CASE("DCE retains RNG calls helper effects loop carries and branch liveness") {
    const std::string source = R"(asset t { size 8x8 output point result
      draw {
        dead=index(3) unused=effect() i=0 x=17
        while(i<3) { x=x+i i=i+1 }
        if (v==0) { x=x+1 } else { x=99 }
        result={x,unit()}
        for j in 0..3 { pixel(j,0,#fff) }
      }
      effect()->i32 { pixel(4,0,#f00) return index(7) }
    })";
    compare(compile(source, true), compile(source, false));
    for (bool optimize : {false,true}) {
        const auto program = compile("asset t { size 8x8 draw { dead = 1 / v } }", optimize);
        CHECK_THROWS_AS(compiler_test::execute(program), Trap);
    }
}
