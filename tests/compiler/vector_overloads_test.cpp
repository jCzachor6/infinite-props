#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "Compiler.hpp"
#include "ips/Parser.hpp"
#include "ips/Printer.hpp"
#include "ips/Semantics.hpp"
#include "support/compiler_support.hpp"

#include <bit>

using namespace iprop;
using namespace compiler_test;

namespace {
std::vector<u8> compileOverload(const std::string& source, bool optimize) {
    const auto program = ips::parse(source);
    ips::checkAsset(program, {});
    return compileProgram(program, {}, optimize);
}
void equalOverloads(const RunOutcome& actual, const RunOutcome& expected) {
    REQUIRE(actual.canvas.width == expected.canvas.width);
    REQUIRE(actual.canvas.height == expected.canvas.height);
    REQUIRE(actual.canvas.pixels == expected.canvas.pixels);
    REQUIRE(actual.outputs.size() == expected.outputs.size());
    for (std::size_t i = 0; i < actual.outputs.size(); ++i) {
        REQUIRE(actual.outputs[i].points.size() == expected.outputs[i].points.size());
        for (std::size_t j = 0; j < actual.outputs[i].points.size(); ++j) {
            CHECK(std::bit_cast<u32>(actual.outputs[i].points[j].x) == std::bit_cast<u32>(expected.outputs[i].points[j].x));
            CHECK(std::bit_cast<u32>(actual.outputs[i].points[j].y) == std::bit_cast<u32>(expected.outputs[i].points[j].y));
        }
    }
}
std::string wrapOverload(const std::string& body) {
    return R"(asset t { size 24x24 output point value output point following
      draw {
        q={2.25+unit(),3.25+unit()} r={5.5+unit(),6.5+unit()}
        c=#12345678
        layer image(alpha=surface.source_over) { rect(1,1,5,6,#abcdef80) }
        region=mask.from(image)
    )" + body + " following={unit(),unit()} } }";
}
}

TEST_CASE("all twenty vec2 overloads preserve scalar pixels and output bits") {
    const std::vector<std::pair<std::string, std::string>> cases{
        {"rect(q,r,c)", "rect(q.x,q.y,r.x,r.y,c)"},
        {"rect(q,r.x,r.y,c)", "rect(q.x,q.y,r.x,r.y,c)"},
        {"rect(q.x,q.y,r,c)", "rect(q.x,q.y,r.x,r.y,c)"},
        {"line(q,r,2,c)", "line(q.x,q.y,r.x,r.y,2,c)"},
        {"oval(q,r,c)", "oval(q.x,q.y,r.x,r.y,c)"},
        {"oval(q,r.x,r.y,c)", "oval(q.x,q.y,r.x,r.y,c)"},
        {"oval(q.x,q.y,r,c)", "oval(q.x,q.y,r.x,r.y,c)"},
        {"value={noise(q,seed),0}", "value={noise(q.x,q.y,seed),0}"},
        {"value={fault(q,seed),0}", "value={fault(q.x,q.y,seed),0}"},
        {"value={noise.grad(q,seed),0}", "value={noise.grad(q.x,q.y,seed),0}"},
        {"value={noise.fbm(q,4,seed),0}", "value={noise.fbm(q.x,q.y,4,seed),0}"},
        {"value={noise.cell(q,seed),0}", "value={noise.cell(q.x,q.y,seed),0}"},
        {"value={noise.ridge(q,4,seed),0}", "value={noise.ridge(q.x,q.y,4,seed),0}"},
        {"value={warp(q,0.5,seed),0}", "value={warp(q.x,q.y,0.5,seed),0}"},
        {"pixel(q,c)", "pixel(int(q.x),int(q.y),c)"},
        {"value=cell.jitter(q,seed)", "value=cell.jitter(int(q.x),int(q.y),seed)"},
        {"value={dither(0.4,q),0}", "value={dither(0.4,int(q.x),int(q.y)),0}"},
        {"surface.paint(mask.offset(region,q),c,blend=surface.source_over)",
         "surface.paint(mask.offset(region,int(q.x),int(q.y)),c,blend=surface.source_over)"},
        {"value={atan2(q),0}", "value={atan2(q.y,q.x),0}"},
        {"value=lerp(q,r,unit())", "t=unit() value={lerp(q.x,r.x,t),lerp(q.y,r.y,t)}"},
    };
    REQUIRE(cases.size() == 20);
    for (const auto& [vector, scalar] : cases) {
        INFO(vector);
        const auto expected = compileOverload(wrapOverload(scalar), false);
        for (const bool optimize : {false, true}) {
            const auto actual = compileOverload(wrapOverload(vector), optimize);
            for (const auto seed : {0u, 1u, 7u, 42u, 0xffffffffu})
                equalOverloads(execute(actual, seed), execute(expected, seed));
            const auto formatted = ips::formatProgram(ips::parse(wrapOverload(vector)));
            equalOverloads(execute(compileOverload(formatted, optimize)), execute(expected));
        }
    }
}

TEST_CASE("vec2 overload arguments evaluate once in signature order") {
    const std::vector<std::pair<std::string, std::string>> cases{
        {"rect(color=tint(),size=point(),position=point())",
         "a=point() b=point() c=tint() rect(a.x,a.y,b.x,b.y,c)"},
        {"rect(color=tint(),h=number(),position=point(),w=number())",
         "a=point() w=number() h=number() c=tint() rect(a.x,a.y,w,h,c)"},
        {"rect(color=tint(),size=point(),y=number(),x=number())",
         "x=number() y=number() b=point() c=tint() rect(x,y,b.x,b.y,c)"},
        {"rect(number(),color=tint(),size=point(),y=number())",
         "x=number() y=number() b=point() c=tint() rect(x,y,b.x,b.y,c)"},
        {"rect(point(),color=tint(),h=number(),w=number())",
         "a=point() w=number() h=number() c=tint() rect(a.x,a.y,w,h,c)"},
        {"line(color=tint(),b=point(),thickness=index(3)+1,a=point())",
         "a=point() b=point() thick=index(3)+1 c=tint() line(a.x,a.y,b.x,b.y,thick,c)"},
        {"oval(color=tint(),radii=point(),center=point())",
         "a=point() b=point() c=tint() oval(a.x,a.y,b.x,b.y,c)"},
        {"oval(number(),radii=point(),y=number(),color=tint())",
         "x=number() y=number() b=point() c=tint() oval(x,y,b.x,b.y,c)"},
        {"value=lerp(t=unit(),b=point(),a=point())",
         "a=point() b=point() t=unit() value={lerp(a.x,b.x,t),lerp(a.y,b.y,t)}"},
        {"value={atan2(v=point()),0}", "a=point() value={atan2(a.y,a.x),0}"},
        {"value={noise.fbm(seed=index(999),octaves=index(6)+1,p=point()),0}",
         "a=point() n=index(6)+1 s=index(999) value={noise.fbm(a.x,a.y,n,s),0}"},
        {"value={dither(p=point(),t=unit()),0}",
         "t=unit() a=point() value={dither(t,int(a.x),int(a.y)),0}"},
    };
    const auto wrap = [](const std::string& body) {
        return "asset t { size 24x24 output point value output point following "
            "point()->vec2{return {unit()*8,unit()*8}} "
            "number()->f32{return unit()*8} "
            "tint()->color{return rgba(index(256),index(256),index(256),index(256))} "
            "draw{" + body + " following={unit(),unit()}}}";
    };
    for (const auto& [vector, scalar] : cases) {
        INFO(vector);
        const auto expected = compileOverload(wrap(scalar), false);
        for (const bool optimize : {false, true}) {
            const auto actual = compileOverload(wrap(vector), optimize);
            const auto formatted = ips::formatProgram(ips::parse(wrap(vector)));
            const auto printed = compileOverload(formatted, optimize);
            for (const auto seed : {0u, 1u, 7u, 0xffffffffu}) {
                equalOverloads(execute(actual, seed), execute(expected, seed));
                equalOverloads(execute(printed, seed), execute(expected, seed));
            }
        }
    }
}

TEST_CASE("integer vec2 coordinates use exact int conversion including nonfinite values") {
    for (const auto* expression : {"-1.75", "-0.0", "0.75", "2.75", "2147483648.0", "-2147483904.0", "z/z", "1.0/z", "-1.0/z"}) {
        const auto wrap = [&](const std::string& body) {
            return "asset t {size 8x8 input f32 z=0 output point value "
                "draw{q={" + std::string(expression) + ",2.75} " + body + "}}";
        };
        INFO(expression);
        for (const auto& [vector, scalar] : std::vector<std::pair<std::string,std::string>>{
            {"pixel(q,#abc)", "pixel(int(q.x),int(q.y),#abc)"},
            {"value={dither(0.5,q),0}", "value={dither(0.5,int(q.x),int(q.y)),0}"},
            {"value=cell.jitter(q,seed)", "value=cell.jitter(int(q.x),int(q.y),seed)"},
            {"layer image(alpha=surface.source_over){pixel(1,1,#fff)} surface.paint(mask.offset(mask.from(image),q),#fff)",
             "layer image(alpha=surface.source_over){pixel(1,1,#fff)} surface.paint(mask.offset(mask.from(image),int(q.x),int(q.y)),#fff)"}}) {
            const auto expected = compileOverload(wrap(scalar), false);
            std::optional<RunOutcome> result;
            std::string trap;
            try { result = execute(expected); }
            catch (const Trap& error) { trap = error.what(); }
            for (const bool optimize : {false, true}) {
                const auto actual = compileOverload(wrap(vector), optimize);
                if (result) equalOverloads(execute(actual), *result);
                else CHECK_THROWS_WITH(execute(actual), trap);
            }
        }
    }
}

TEST_CASE("nested and sequential vector overloads reuse temporary slots") {
    std::string body = "q={1,2} ";
    for (unsigned i = 0; i < 1024; ++i)
        body += "q=lerp(lerp(q,{3,4},t),lerp({5,6},q,t),t) ";
    const auto bytes = compileOverload("asset t{size 1x1 input f32 t=0.25 output point value draw{" + body + "value=q}}", false);
    const auto file = loadFile(bytes);
    CHECK(file.code.entry().locals.size() <= 8);
    REQUIRE_NOTHROW(execute(bytes));
}

TEST_CASE("vec2 lerp and atan2 retain scalar boundary bits and support constants") {
    for (const auto* point : {"{-0.0,0.0}", "{0.0,-0.0}", "{-3.0,4.0}", "{3.4028234663852886e38,-3.4028234663852886e38}"}) {
        for (const auto* amount : {"-1.0", "0.0", "0.25", "1.0", "2.0", "0.0/0.0"}) {
            const std::string a = point, t = amount;
            const auto wrap = [](const std::string& declarations, const std::string& body) {
                return "asset t{size 1x1 output point value output point angle " + declarations + " draw{" + body + "}}";
            };
            const std::string inputs = "q=" + a + " r=-q t=" + t + " ";
            const auto expected = compileOverload(wrap("", inputs + "value={lerp(q.x,r.x,t),lerp(q.y,r.y,t)} angle={atan2(q.y,q.x),0}"), false);
            for (const bool optimize : {false, true}) {
                equalOverloads(execute(compileOverload(wrap("", inputs + "value=lerp(q,r,t) angle={atan2(q),0}"), optimize)), execute(expected));
                equalOverloads(execute(compileOverload(wrap("const vec2 q=" + a + " const vec2 r=-q const f32 t=" + t +
                    " const vec2 p=lerp(q,r,t) const f32 ang=atan2(q)", "value=p angle={ang,0}"), optimize)), execute(expected));
            }
        }
    }
}

TEST_CASE("vec2 overloads reject invalid types and keep scalar signatures strict") {
    for (const auto* body : {"pixel(1.5,2,#fff)", "line({1,2},{3,4},1.5,#fff)",
         "noise({1,2},1.0)", "rect({1,2},#fff,#fff)", "rect(position={1,2},size={3,4},x=1,color=#fff)",
         "rect(position={1,2},size={3,4})", "atan2(#fff)", "lerp({1,2},2,0.5)",
         "mask.offset({1,2},{3,4})", "dither(0.5,#fff)", "cell.jitter({1,2},0.5)"}) {
        INFO(body);
        CHECK_THROWS_AS(compileOverload("asset t{size 8x8 draw{" + std::string(body) + "}}", true), ips::SourceError);
    }
}
