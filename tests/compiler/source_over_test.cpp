#include <catch2/catch_test_macros.hpp>
#include "Compiler.hpp"
#include "SourceCompilation.hpp"
#include "iprop/Compositor.hpp"
#include "iprop/VM.hpp"
#include "iprop/VmInternals.hpp"
#include "ips/Parser.hpp"
#include "ips/Printer.hpp"
#include "ips/Semantics.hpp"
#include "ipa.hpp"
#include "ipdis.hpp"

#include <bit>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>

using namespace iprop;
namespace {
std::vector<u8> sourceOverProgram(const std::string& source, bool optimize = true) {
    const auto program = ips::parse(source);
    ips::checkAsset(program, {});
    return compileProgram(program, {}, optimize);
}
GenerationResult runSourceOver(const std::vector<u8>& bytes, u32 seed = 26, u32 variant = 0,
        u32 width = 0, u32 height = 0) {
    const auto file = loadFile(bytes); validate(file);
    VM vm(file, {seed,width,height,{{"v",{InputType::U32,{},0.f,variant}}}}); vm.run(); return vm.result();
}
std::vector<u8> sourceOverTree(const std::map<std::string, std::string>& files, bool optimize) {
    SourceCompilationOptions options; options.optimize = optimize;
    const auto result = compileSource("root.ips", [&](std::string_view name) { return files.at(std::string(name)); }, options);
    for (const auto& diagnostic : result.diagnostics) INFO(diagnostic.message);
    REQUIRE(result.succeeded()); return result.bytes;
}
}

TEST_CASE("source-over agrees with exact rational pins for every alpha pair", "[source-over]") {
    const auto path = std::filesystem::path(__FILE__).parent_path().parent_path() / "fixtures/source-over-v0.6.rgba";
    std::ifstream input(path, std::ios::binary);
    const std::vector<u8> expected{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    REQUIRE(expected.size() == 256u * 256u * 4u);
    for (unsigned ad = 0; ad < 256; ++ad) for (unsigned as = 0; as < 256; ++as) {
        const auto actual = composePixel({201,4,172,static_cast<u8>(ad)}, {17,213,88,static_cast<u8>(as)}, CompositionMode::SourceOver);
        const auto at = (ad * 256u + as) * 4u;
        INFO("destination alpha=" << ad << ", source alpha=" << as);
        REQUIRE(actual == Color{expected[at],expected[at+1],expected[at+2],expected[at+3]});
    }
    CHECK(composePixel({123,77,91,0},{19,23,29,0},CompositionMode::SourceOver) == Color{});
    CHECK(composePixel({255,0,255,255},{0,1,2,255},CompositionMode::SourceOver) == Color{0,1,2,255});
    // Exact half-way RGB (127.5) rounds up.
    CHECK(composePixel({0,0,0,2},{254,254,254,2},CompositionMode::SourceOver) == Color{128,128,128,4});
    CHECK(composePixel({17,34,51,0},{255,0,0,128},CompositionMode::Legacy) == Color{136,16,25,0});
}

TEST_CASE("source-over captures round each draw and paint without changing root defaults", "[source-over]") {
    const std::string source = R"(asset t{size 6x1
      echo(image:layer)->layer{return image}
      draw{
        pixel(0,0,#11223380)
        layer image(alpha=surface.source_over){
          pixel(0,0,#11223380) pixel(0,0,#ff000080)
          pixel(1,0,#11223380) pixel(2,0,#12345600)
          pixel(3,0,#112233ff)
        }
        surface.paint(image)
        surface.paint(echo(image),blend=surface.source_over)
        pixel(4,0,#11223380)
        layer hidden{pixel(5,0,#11223380)}
        surface.paint(hidden,blend=surface.source_over)
      }})";
    for (const bool optimize : {false,true}) {
        const auto bytes = sourceOverProgram(source,optimize), restored = assemble(disassemble(bytes));
        const auto actual = runSourceOver(bytes);
        CHECK(actual.image.at(0,0) == Color{176,11,17,192});
        CHECK(actual.image.at(1,0) == Color{17,34,51,128});
        CHECK(actual.image.at(2,0) == Color{});
        CHECK(actual.image.at(3,0) == Color{17,34,51,255});
        // Painting the full transparent source canonicalizes hidden zero-alpha RGB.
        CHECK(actual.image.at(4,0) == Color{});
        CHECK(actual.image.at(5,0) == Color{});
        CHECK(runSourceOver(restored).image.pixels == actual.image.pixels);
        const auto formatted = ips::formatProgram(ips::parse(source));
        CHECK(ips::formatProgram(ips::parse(formatted)) == formatted);
        CHECK(runSourceOver(sourceOverProgram(formatted,optimize)).image.pixels == actual.image.pixels);
        CHECK(disassemble(bytes).find("surface.paint 2") != std::string::npos);
    }
}

TEST_CASE("helpers inherit alpha and paint overrides only one operation", "[source-over]") {
    const std::string source = R"(asset t{size 5x1 output point state
      mark(x:i32){pixel(x,0,#11223380)}
      image()->layer{layer nested{mark(0)}return nested}
      draw{
        layer outer(alpha=surface.source_over){
          mark(1) surface.paint(image())
          layer legacy(alpha=surface.legacy){mark(2)}
          surface.paint(legacy)
          surface.paint(image(),blend=surface.legacy)
          mark(3)
        }
        surface.paint(outer,blend=surface.source_over)
        mark(4) state={seed&65535,seed>>16}
      }})";
    for (const bool optimize : {false,true}) {
        const auto result = runSourceOver(sourceOverProgram(source,optimize));
        CHECK(result.image.at(0,0) == Color{17,34,51,128});
        CHECK(result.image.at(1,0) == Color{17,34,51,128});
        CHECK(result.image.at(2,0) == Color{});
        CHECK(result.image.at(3,0) == Color{17,34,51,128});
        CHECK(result.image.at(4,0) == Color{8,17,25,0});
        CHECK(result.outputs[0].points[0].x == 26.f); CHECK(result.outputs[0].points[0].y == 0.f);
    }
}

TEST_CASE("source-over grouping preserves each RGBA8 rounding boundary", "[source-over]") {
    const std::string source = R"(asset t{size 2x1 draw{
      layer image(alpha=surface.source_over){
        pixel(0,0,#11223380) pixel(0,0,#11223380) pixel(0,0,#ff000080)
        pixel(1,0,#11223380)
        layer group{pixel(1,0,#11223380)pixel(1,0,#ff000080)}
        surface.paint(group)
      }
      surface.paint(image,blend=surface.source_over)
    }})";
    for (const bool optimize : {false,true}) {
        const auto result = runSourceOver(sourceOverProgram(source,optimize));
        CHECK(result.image.at(0,0) == Color{153,15,22,224});
        CHECK(result.image.at(1,0) == Color{154,14,22,224});
    }
}

TEST_CASE("weather operates on inherited targets without changing source-over alpha", "[source-over]") {
    for (const bool optimize : {false,true}) {
        const auto result = runSourceOver(sourceOverProgram(R"(asset t{size 3x2 output point state
          age(){weather(7,20)} draw{layer image(alpha=surface.source_over){rect(0,0,3,2,#11223380)age()}
          surface.paint(image,blend=surface.source_over) state={seed&65535,seed>>16}}})",optimize));
        for (const auto pixel : result.image.pixels) CHECK(pixel.a == 128);
        CHECK(result.outputs[0].points[0].x == 26.f); CHECK(result.outputs[0].points[0].y == 0.f);
    }
}

TEST_CASE("return break continue and traps restore the full target mode", "[source-over]") {
    const std::string source = R"(asset t{size 5x2
      early(){layer gone(alpha=surface.legacy){pixel(0,0,#fff)return;} }
      draw{
        layer outer(alpha=surface.source_over){
          early() pixel(0,0,#11223380)
          for i in 0..3{layer skipped(alpha=surface.legacy){if(i==0){continue}break;}}
          pixel(1,0,#11223380)
          layer inner{pixel(2,0,#11223380)}surface.paint(inner)
        }
        surface.paint(outer,blend=surface.source_over)
        for j in 0..2{layer image(alpha=surface.source_over){pixel(j,1,#ff000080)continue;}}
        surface.paint(image,blend=surface.source_over)
        pixel(3,0,#11223380)
        layer discarded(alpha=surface.source_over){pixel(4,0,#fff) return;}
      }})";
    for (const bool optimize : {false,true}) {
        const auto result = runSourceOver(sourceOverProgram(source,optimize));
        for (const u32 x : {0u,1u,2u}) CHECK(result.image.at(x,0) == Color{17,34,51,128});
        CHECK(result.image.at(3,0) == Color{8,17,25,0}); CHECK(result.image.at(4,0) == Color{});
        CHECK(result.image.at(0,1) == Color{}); CHECK(result.image.at(1,1) == Color{255,0,0,128});
        const auto file = loadFile(sourceOverProgram("asset t{ input i32 v = 0 size 1x1 draw{pixel(0,0,#11223380) layer outer(alpha=surface.source_over){layer inner(alpha=surface.legacy){x=1/(v-v)}}}}",optimize));
        VM vm(file,{}); CHECK_THROWS_AS(vm.run(),Trap); CHECK(vm.canvas().at(0,0) == Color{8,17,25,0});
        CHECK_THROWS_AS(vm.result(),std::logic_error); CHECK_THROWS_AS(vm.takeResult(),std::logic_error);
    }
}

TEST_CASE("part canvases and blits inherit source-over across nested D4 and scale", "[source-over]") {
    for (const bool optimize : {false,true}) {
        std::string calls;
        for (u32 v = 0; v < 8; ++v) calls += "if(v=="+std::to_string(v)+"){child.draw(seed,at={7.5,6.5},rot="+std::to_string(v%4*90)+",mirror_x="+std::to_string(v/4)+")} ";
        const std::map<std::string,std::string> sources{
            {"root.ips",R"(import child from "child.ips" asset t{input i32 v=0 size 16x12 output point p output point state
              draw{layer image(alpha=surface.source_over){)"+calls+R"(p=child.outputs.p}surface.paint(image,blend=surface.source_over) pixel(15,11,#11223380) state={seed&65535,seed>>16}}})"},
            {"child.ips",R"(import leaf from "leaf.ips" asset c{size 7x5 origin 2,1 output point p
              draw{leaf.draw(seed,at={3,2},rot=90,mirror_y=1) p=leaf.outputs.p pixel(6,4,#11223380)}})"},
            {"leaf.ips",R"(asset leaf{size 3x2 origin 1,0 output point p
              draw{pixel(0,0,#ff000080) pixel(2,1,#0000ff80) layer hidden(alpha=surface.legacy){pixel(1,0,#fff)}
                p={1.0 to 2.0,0}}})"},
        };
        const auto bytes = sourceOverTree(sources,optimize);
        for (const auto size : std::vector<RasterPoint>{{16,12},{32,12},{8,24}})
            for (u32 variant = 0; variant < 8; ++variant) for (const u32 seed : {0u,26u,0xffffffffu}) {
                const auto result = runSourceOver(bytes,seed,variant,size.x,size.y);
                std::size_t transparent = 0, visible = 0, legacy = 0;
                for (const auto pixel : result.image.pixels) {
                    if (pixel.a == 128) { ++visible; CHECK((pixel == Color{255,0,0,128} || pixel == Color{0,0,255,128} || pixel == Color{17,34,51,128})); }
                    else if (pixel == Color{8,17,25,0}) ++legacy;
                    else { ++transparent; CHECK(pixel == Color{}); }
                }
                CHECK(visible <= 3); CHECK(legacy == (size.x == 8 ? 0 : 1)); CHECK(transparent > 0);
                REQUIRE(result.outputs.size() == 4);
                CHECK(result.outputs[1].points[0].x == static_cast<f32>(seed & 65535));
                CHECK(result.outputs[1].points[0].y == static_cast<f32>(seed >> 16));
                CHECK(std::bit_cast<u32>(result.outputs[0].points[0].x) == std::bit_cast<u32>(result.outputs[2].points[0].x));
                CHECK(std::bit_cast<u32>(result.outputs[0].points[0].y) == std::bit_cast<u32>(result.outputs[2].points[0].y));
            }
    }
}

TEST_CASE("paint canonicalizes transparent hidden RGB only in source-over", "[source-over]") {
    const auto bytes = sourceOverProgram("asset t{size 1x1 draw{layer image{}surface.paint(image)}}",false);
    const auto file = loadFile(bytes);
    detail::Execution ex(file); ex.target = RenderTarget::create(1,1,{1,1}); ex.frames.push_back({}); ex.fuel = 100;
    auto source = RenderTarget::create(1,1,{1,1}); source.canvas().at(0,0) = {255,7,9,0};
    const auto handle = source.view(); ex.canvas().at(0,0) = {17,23,29,0};
    detail::paintSurface(ex,handle); CHECK(ex.canvas().at(0,0) == Color{17,23,29,0});
    detail::paintSurface(ex,handle,CompositionMode::SourceOver); CHECK(ex.canvas().at(0,0) == Color{});
    CHECK(ex.target.mode == CompositionMode::Legacy); CHECK(ex.fuel == 100);
    ex.canvas().at(0,0) = {17,23,29,128};
    detail::paintSurface(ex,handle,CompositionMode::SourceOver); CHECK(ex.canvas().at(0,0) == Color{17,23,29,128});
    CHECK(validCompositionMode(2)); CHECK_FALSE(validCompositionMode(3)); CHECK_FALSE(validCompositionMode(257));
    ex.push(handle); CHECK_THROWS_AS(detail::stepSurfaces(ex,{Opcode::SurfacePaint,0,257}),Trap);
}
