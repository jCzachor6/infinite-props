#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <algorithm>
#include <bit>
#include <map>
#include <numeric>
#include <string>
#include <vector>

#include "Compiler.hpp"
#include "iprop/Debug.hpp"
#include "iprop/VM.hpp"
#include "ips/Parser.hpp"
#include "ips/Printer.hpp"
#include "ips/Semantics.hpp"
#include "ips/SourceError.hpp"
#include "ipa.hpp"
#include "ipdis.hpp"

#include "support/compiler_support.hpp"

using namespace iprop;
using namespace compiler_test;

TEST_CASE("named selection ramp range and polygon permutations preserve absolute RNG effects",
          "[call-effects]") {
    struct EffectCase {
        std::string function;
        std::vector<std::string> names;
        std::vector<std::string> expressions;
        std::vector<int> order;
        std::string reference;
        bool returnsValue = true;
    };
    const std::vector<EffectCase> cases{
        {"range", {"a", "b"}, {"mark(1)", "mark(3)"}, {1, 3}, "value=range(1,3);"},
        {"pick", {"value0", "value1", "value2"},
            {"mark(1)", "mark(2)", "mark(3)"}, {1, 2, 3}, "value=values[index(3)];"},
        {"choose", {"seed", "value0", "value1", "value2"},
            {"mark(0)", "mark(1)", "mark(2)", "mark(3)"}, {0, 1, 2, 3},
            "stream_push(0); value=values[index(3)]; stream_pop();"},
        {"ramp", {"palette", "t"}, {"values", "mark(1)"}, {1}, "value=ramp(values,1);"},
        {"poly", {"color", "x0", "y0", "x1", "y1", "x2", "y2"},
            {"tint()", "mark(1)", "mark(2)", "mark(3)", "mark(4)", "mark(5)", "mark(6)"},
            {1, 2, 3, 4, 5, 6, 70}, "poly(#abc,1,2,3,4,5,6);", false}
    };
    const auto source = [](const std::string& body) {
        return "asset t {size 8x8 output list point trace palette values=[1.0,2.0,3.0] "
            "draw {" + body + "} "
            "mark(tag:i32)->i32 {trace={tag,unit()}; return tag;} "
            "tint()->color {trace={70,unit()}; return #abc;} }";
    };
    const auto compile = [](const std::string& text, bool optimize) {
        const auto program = ips::parse(text);
        ips::checkAsset(program, {});
        return compileProgram(program, {}, optimize);
    };
    for (const auto& test : cases) {
        INFO(test.function);
        const std::string after = (test.returnsValue ? "trace={value,unit()};" : "") +
            std::string("trace={99,unit()};");
        const auto callBody = [&](const std::string& arguments) {
            return (test.returnsValue ? "value=" : "") + test.function + "(" + arguments + ");" + after;
        };
        std::string positional;
        for (const auto& expression : test.expressions) {
            if (!positional.empty()) positional += ",";
            positional += expression;
        }
        std::string referenceBody;
        for (const auto tag : test.order)
            referenceBody += "trace={" + std::to_string(tag) + ",unit()};";
        referenceBody += test.reference + after;
        std::vector<std::size_t> permutation(test.names.size());
        std::iota(permutation.begin(), permutation.end(), 0);
        std::vector<std::vector<std::size_t>> permutations;
        if (permutation.size() <= 4) {
            do { permutations.push_back(permutation); }
            while (std::next_permutation(permutation.begin(), permutation.end()));
        } else {
            // Both directions and every cyclic position cover polygon's
            // exceptional coordinates-before-colour order without 7! builds.
            for (std::size_t rotation = 0; rotation < permutation.size(); ++rotation) {
                permutations.push_back(permutation);
                auto reverse = permutation;
                std::reverse(reverse.begin(), reverse.end());
                permutations.push_back(std::move(reverse));
                std::rotate(permutation.begin(), permutation.begin() + 1, permutation.end());
            }
        }
        for (const bool optimize : {false, true}) {
            const auto baseline = compile(source(callBody(positional)), optimize);
            const auto reference = compile(source(referenceBody), optimize);
            for (const auto& slots : permutations) {
                for (const bool mixed : {false, true}) {
                    std::string arguments;
                    for (const auto slot : slots) {
                        if (!arguments.empty()) arguments += ",";
                        if (!mixed || slot != 0) arguments += test.names[slot] + "=";
                        arguments += test.expressions[slot];
                    }
                    INFO("optimize=" << optimize << " arguments=" << arguments);
                    const auto bytes = compile(source(callBody(arguments)), optimize);
                    CHECK(stripDebug(bytes) == stripDebug(baseline));
                    const auto formatted = ips::formatProgram(ips::parse(source(callBody(arguments))));
                    CHECK(ips::formatProgram(ips::parse(formatted)) == formatted);
                    CHECK(stripDebug(compile(formatted, optimize)) == stripDebug(bytes));
                    for (const u32 seed : {0u, 7u, 26u, 0xffffffffu}) {
                        INFO("seed=" << seed);
                        const auto actual = execute(bytes, seed);
                        const auto expected = execute(reference, seed);
                        CHECK(actual.canvas.pixels == expected.canvas.pixels);
                        REQUIRE(actual.outputs.size() == 1);
                        REQUIRE(expected.outputs.size() == 1);
                        const auto& points = actual.outputs[0].points;
                        const auto& expectedPoints = expected.outputs[0].points;
                        REQUIRE(points.size() == test.order.size() + (test.returnsValue ? 2 : 1));
                        REQUIRE(points.size() == expectedPoints.size());
                        for (std::size_t i = 0; i < points.size(); ++i) {
                            CHECK(std::bit_cast<u32>(points[i].x) == std::bit_cast<u32>(expectedPoints[i].x));
                            CHECK(std::bit_cast<u32>(points[i].y) == std::bit_cast<u32>(expectedPoints[i].y));
                        }
                        CHECK(points.back().x == 99.f);
                    }
                }
            }
        }
    }
}

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
          child.draw(note(15), at={note(18),note(19)},
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
    const std::vector<i32> order{1,2,30,3,4,5,6,7,8,9,10,11,12,70,13,14,15,18,19,20};
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
        draw{child.draw(seed,at={0,0});}
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
