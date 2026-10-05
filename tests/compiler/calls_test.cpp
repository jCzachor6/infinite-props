#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <bit>
#include <map>
#include <string>
#include <vector>

#include "Compiler.hpp"
#include "iprop/VM.hpp"
#include "ips/Parser.hpp"
#include "ips/Semantics.hpp"
#include "ips/SourceError.hpp"
#include "ipa.hpp"
#include "ipdis.hpp"

#include "support/compiler_support.hpp"

using namespace iprop;
using namespace compiler_test;

TEST_CASE("resolved calls preserve argument evaluation order and the RNG stream") {
    const auto part = ips::parse(R"(asset child { size 8x8
      input f32 first = 1.0
      input f32 second = 2.0
      draw { }
    })");
    const auto root = ips::parse(R"(import child from "child.ips"
      asset root { size 8x8 output list point trace palette p = [1.0]
        draw {
          sink(note(1), note(2))
          lerp(note(3), note(4), note(5))
          round(note(6))
          poly(tint(), note(7), note(8), note(9), note(10), note(11), note(12))
          child.draw(note(15), note(16), note(17), at={note(18),note(19)},
                     second=note(14), first=note(13))
          ramp(p, note(20))
        }
        note(tag:i32) -> i32 { trace = {tag, unit()} return 0 }
        tint() -> color { trace = {70, unit()} return #000000 }
        sink(x:f32,y:f32) { trace = {30, unit()} }
      })");
    ips::checkAsset(part, {});
    ips::checkAsset(root, {{"child",partInfoOf(part.asset)}});
    const auto actual = execute(compileProgram(root, {{"child",&part}}));
    const std::vector<i32> order{1,2,30,3,4,5,6,7,8,9,10,11,12,70,13,14,15,16,17,18,19,20};
    std::string body;
    for (const auto tag : order) body += "trace = {" + std::to_string(tag) + ", unit()}\n";
    const auto reference = execute(compileChecked("asset t { size 8x8 output list point trace "
        "draw {" + body + "} }"));
    REQUIRE(actual.outputs[0].points.size() == order.size());
    for (std::size_t i = 0; i < order.size(); ++i) {
        INFO("effect " << i);
        CHECK(actual.outputs[0].points[i].x == reference.outputs[0].points[i].x);
        CHECK(std::bit_cast<u32>(actual.outputs[0].points[i].y) ==
              std::bit_cast<u32>(reference.outputs[0].points[i].y));
    }
}

TEST_CASE("named helper arguments preserve declaration order widening and RNG effects") {
    const std::string prefix=R"(asset t {size 8x8 output list point trace draw { )";
    const std::string suffix=R"( }
        note(tag:i32)->i32 {trace={tag,unit()}; return tag;}
        tint()->color {trace={3,unit()}; return #9dc;}
        paint(x:f32,y:f32,tint:color){trace={4,unit()}; rect(x,y,1,1,tint);}
    })";
    const auto positional=ips::parse(prefix+"paint(note(1),note(2),tint()); trace={5,unit()};"+suffix);
    const auto named=ips::parse(prefix+"paint(tint=tint(),y=note(2),x=note(1)); trace={5,unit()};"+suffix);
    const auto mixed=ips::parse(prefix+"paint(note(1),tint=tint(),y=note(2)); trace={5,unit()};"+suffix);
    for (const auto* program : {&positional,&named,&mixed}) CHECK_NOTHROW(ips::checkAsset(*program,{}));
    for (bool optimize : {false,true}) {
        const auto reference=compileProgram(positional,{},optimize);
        CHECK(compileProgram(named,{},optimize)==reference);
        CHECK(compileProgram(mixed,{},optimize)==reference);
        for (u32 seed : {0u,7u,0xffffffffu}) {
            const auto actual=execute(reference,seed);
            REQUIRE(actual.outputs[0].points.size()==5);
            for (std::size_t i=0;i<5;++i) CHECK(actual.outputs[0].points[i].x==i+1);
            CHECK(actual.canvas.at(1,2)==Color{153,221,204,255});
        }
    }
}

TEST_CASE("named helpers inside imported parts bind their own parameter names") {
    const auto root=ips::parse(R"(import child from "child.ips" asset t {size 8x8
        draw{child.draw(seed,v,a,at={0,0});}
        paint(rootName:i32) {pixel(rootName,7,#f00);}
    })");
    const auto positional=ips::parse(R"(asset child {size 8x8 draw{paint(2,#9dc);}
        paint(x:i32,tint:color){pixel(x,0,tint);}
    })");
    const auto named=ips::parse(R"(asset child {size 8x8 draw{paint(tint=#9dc,x=2);}
        paint(x:i32,tint:color){pixel(x,0,tint);}
    })");
    ips::checkAsset(named,{});
    ips::checkAsset(root,{{"child",ips::partInfoOf(named.asset)}});
    for (bool optimize : {false,true}) {
        const auto reference=compileProgram(root,{{"child",&positional}},optimize);
        const auto actual=compileProgram(root,{{"child",&named}},optimize);
        CHECK(actual==reference);
        CHECK(execute(actual).canvas.at(2,0)==Color{153,221,204,255});
        CHECK(execute(actual).canvas.at(2,7)==Color{0,0,0,0});
    }
}
