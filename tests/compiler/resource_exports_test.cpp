#include <catch2/catch_test_macros.hpp>
#include "SourceCompilation.hpp"
#include "iprop/Compression.hpp"
#include "iprop/Debug.hpp"
#include "iprop/VmInternals.hpp"
#include "iprop/VM.hpp"
#include "ips/Parser.hpp"
#include "ips/Printer.hpp"
#include "ipa.hpp"
#include "ipdis.hpp"

#include <array>
#include <map>
#include <string>

using namespace iprop;
namespace {
using Sources = std::map<std::string, std::string>;
std::vector<u8> compileExports(const Sources& sources, bool optimize = false) {
    SourceCompilationOptions options; options.optimize = optimize; options.compress = false;
    const auto result = compileSource("root.ips", [&](std::string_view path) { return sources.at(std::string(path)); }, options);
    std::string diagnostics;
    for (const auto& diagnostic : result.diagnostics) diagnostics += diagnostic.message + "\n";
    INFO(diagnostics);
    REQUIRE(result.succeeded()); return result.bytes;
}
GenerationResult generateExports(const std::vector<u8>& bytes, u32 width = 0, u32 height = 0) {
    const auto file = loadFile(bytes); validate(file); VM vm(file, {26,width,height,{}}); vm.run(); return vm.result();
}
std::string transform(u8 kind) {
    constexpr std::array rotations{0,90,180,270,0,0,90,90};
    return ",rot=" + std::to_string(rotations[kind]) + ",mirror_x=" + std::to_string(kind == 4 || kind == 7) +
        ",mirror_y=" + std::to_string(kind == 5 || kind == 6);
}
// Independent integer formulas, deliberately not calling D4Transform.
RasterPoint oracle(u8 kind, RasterPoint p, RasterPoint extent) {
    switch (kind) {
    case 0: return p; case 1: return {p.y,extent.x-1-p.x};
    case 2: return {extent.x-1-p.x,extent.y-1-p.y};
    case 3: return {extent.y-1-p.y,p.x}; case 4: return {extent.x-1-p.x,p.y};
    case 5: return {p.x,extent.y-1-p.y}; case 6: return {p.y,p.x};
    default: return {extent.y-1-p.y,extent.x-1-p.x};
    }
}
void set16(std::vector<u8>& bytes, std::size_t at, u16 value) { bytes.at(at)=static_cast<u8>(value);bytes.at(at+1)=static_cast<u8>(value>>8); }
void set32(std::vector<u8>& bytes, std::size_t at, u32 value) { for(u32 i=0;i<4;++i)bytes.at(at+i)=static_cast<u8>(value>>(8*i)); }
}

TEST_CASE("resource exports map shared views once for every D4 pair including outside bounds", "[resource-exports]") {
    auto file = loadFile(compileExports({{"root.ips","asset t{size 30x20 output layer image draw{}}"}}));
    detail::Execution ex(file); ex.target=RenderTarget::create(30,20,{1,1}); ex.frames.emplace_back();ex.fuel=10000;
    auto buffer=RenderTarget::create(3,2,{1,1}).buffer;
    for (u32 y=0;y<2;++y) for(u32 x=0;x<3;++x)buffer->raster.at(x,y)={static_cast<u8>(1+y*3+x),0,0,255};
    // This internal test uses a part-owned slot to exercise exactly one export
    // mapping. Production files validate ownership before constructing a VM.
    file.resourceExports[0].owner=0;
    ex.resourceSlots={LayerValue{}};
    for (u8 inner=0;inner<8;++inner) for(u8 outer=0;outer<8;++outer) {
        detail::PartCallState call;call.partIndex=0;call.render.transform=*D4Transform::fromKind(outer);
        call.render.sourceExtent={7,5};call.render.targetOrigin={11,-9};ex.partCalls={call};
        const LayerValue value{buffer,{*D4Transform::fromKind(inner),{-2,4}}};
        ex.push(value);REQUIRE(detail::stepResourceExports(ex,{Opcode::OutLayer,0,0}));
        const auto exported=std::get<LayerValue>(ex.resourceSlots[0]);CHECK(exported.buffer==value.buffer);
        for (i32 y=0;y<2;++y) for(i32 x=0;x<3;++x) {
            auto expected=oracle(inner,{x,y},{3,2});expected.x-=2;expected.y+=4;
            expected=oracle(outer,expected,{7,5});expected.x+=11;expected.y-=9;
            auto actual=exported.mapping.transform.mapPixel({x,y},{3,2});
            actual.x+=exported.mapping.origin.x;actual.y+=exported.mapping.origin.y;
            CHECK(actual==expected);
        }
        CHECK(ex.resources.bytes==0);CHECK(ex.fuel==10000);
    }
    ex.partCalls.back().render.transform={};ex.partCalls.back().render.targetOrigin={2147483647,0};
    ex.push(LayerValue{buffer,{}});CHECK_THROWS_AS(detail::stepResourceExports(ex,{Opcode::OutLayer,0,0}),Trap);
    CHECK(ex.resources.bytes==0);
}

TEST_CASE("three asset levels propagate layer and mask exports through all D4 combinations", "[resource-exports]") {
    for (u8 leaf=0;leaf<8;++leaf) for(u8 middle=0;middle<8;++middle) for(const bool optimize:{false,true}) {
        const Sources exported{
            {"root.ips","import middle from \"middle.ips\" asset t{size 24x20 draw{middle.draw(seed,at={9,7}"+transform(middle)+")"
             "surface.paint(middle.outputs.image,blend=surface.legacy)}}"},
            {"middle.ips","import leaf from \"leaf.ips\" asset t{size 9x7 origin 2,1 output layer image output mask region draw{"
             "leaf.draw(seed,at={3,2}"+transform(leaf)+")image=leaf.outputs.image region=leaf.outputs.region}}"},
            {"leaf.ips","asset t{size 3x2 origin 1,0 output layer image output mask region draw{layer captured(alpha=surface.legacy){"
             "pixel(0,0,#11223380)pixel(2,1,#556677ff)}image=captured region=mask.from(captured)}}"}};
        Sources reference=exported;
        reference["root.ips"]="import middle from \"middle.ips\" asset t{size 24x20 draw{middle.draw(seed,at={9,7}"+transform(middle)+")}}";
        reference["middle.ips"]="import leaf from \"leaf.ips\" asset t{size 9x7 origin 2,1 draw{leaf.draw(seed,at={3,2}"+transform(leaf)+")}}";
        reference["leaf.ips"]="asset t{size 3x2 origin 1,0 draw{pixel(0,0,#11223380)pixel(2,1,#556677ff)}}";
        const auto actual=compileExports(exported,optimize), expected=compileExports(reference,optimize);
        Sources mask=exported;
        mask["root.ips"]="import middle from \"middle.ips\" asset t{size 24x20 draw{middle.draw(seed,at={9,7}"+transform(middle)+")"
            "clip(middle.outputs.region){rect(-1,-1,26,22,#ffffffff)}}}";
        const auto masked=compileExports(mask,optimize);
        for (const auto size: {RasterPoint{24,20},RasterPoint{48,40},RasterPoint{36,50}}) {
            const auto image=generateExports(actual,static_cast<u32>(size.x),static_cast<u32>(size.y));
            const auto base=generateExports(expected,static_cast<u32>(size.x),static_cast<u32>(size.y));
            const auto region=generateExports(masked,static_cast<u32>(size.x),static_cast<u32>(size.y));
            CHECK(image.image.pixels==base.image.pixels);
            for(std::size_t i=0;i<image.image.pixels.size();++i)
                CHECK(region.image.pixels[i].a==image.image.pixels[i].a);
            CHECK(image.outputs.empty()); CHECK(region.outputs.empty());
        }
    }
}

TEST_CASE("export snapshots survive later calls no-write calls and slot replacement", "[resource-exports]") {
    const Sources sources{
        {"root.ips",R"(import child from "child.ips" asset t{size 8x2 output mask hidden output point p draw{
          child.draw(1,at={1,0}) first=child.outputs.image old=child.outputs.region
          child.draw(2,at={4,0}) second=child.outputs.image
          child.draw(3,at={7,0}) retained=child.outputs.image
          surface.paint(first)surface.paint(second)surface.paint(retained)
          clip(old){rect(0,0,8,2,#00ff00)}p=child.outputs.p
        }})"},
        {"child.ips",R"(asset t{size 2x1 output point p output layer image output mask region draw{
          if(seed!=3){layer captured{pixel(0,0,rgb(seed*50,0,0))}image=captured region=mask.from(captured)} p={1,0}
        }})"}};
    for(const bool optimize:{false,true}){
        const auto bytes=compileExports(sources,optimize);const auto file=loadFile(bytes);validate(file);
        CHECK(file.meta.outputs.size()==1);CHECK(file.code.parts[0].outputs.size()==1);CHECK(file.resourceExports.size()==3);
        const auto result=generateExports(bytes);CHECK(result.outputs.size()==2);
        CHECK(result.image.at(1,0)==Color{0,255,0,255});CHECK(result.image.at(4,0)==Color{100,0,0,255});
        CHECK(result.image.at(7,0)==Color{});CHECK(result.outputs[0].points[0].x==8);
        CHECK(result.outputs[1].points[0].x==8);
        const auto restored=assemble(disassemble(bytes));CHECK(generateExports(restored).image.pixels==result.image.pixels);
        CHECK(generateExports(stripDebug(bytes)).image.pixels==result.image.pixels);
        CHECK(generateExports(compressFile(bytes)).image.pixels==result.image.pixels);
    }
}

TEST_CASE("surface storage is freed at the last handle and accounting tokens cannot dangle", "[resource-exports]") {
    ResourceArena arena;arena.charge(152);auto buffer=arena.makeSurface(152);std::weak_ptr<const SurfaceBuffer> weak=buffer;
    LayerValue image{buffer,{}};MaskValue mask{buffer,{}};buffer.reset();
    CHECK(arena.bytes==152);image={};CHECK_FALSE(weak.expired());mask={};CHECK(weak.expired());CHECK(arena.bytes==0);
    for(int i=0;i<1000;++i){arena.charge(128);auto fresh=arena.makeSurface(128);CHECK(arena.bytes==128);}
    CHECK(arena.bytes==0);
    arena.charge(128);auto retained=arena.makeSurface(128);arena.clear();CHECK(arena.bytes==0);
    arena.charge(128);auto next=arena.makeSurface(128);retained.reset();CHECK(arena.bytes==128);next.reset();CHECK(arena.bytes==0);
    LayerValue outside;
    {ResourceArena temporary;temporary.charge(128);outside={temporary.makeSurface(128),{}};}
    CHECK_NOTHROW(outside={});
}

TEST_CASE("resource exports trap on unset slots wrong last part and absent local handles", "[resource-exports]") {
    for(const std::string body:{"child.draw(seed,at={0,0})surface.paint(child.outputs.image)",
        "child.draw(1,at={0,0})other.draw(1,at={0,0})surface.paint(child.outputs.image)"}){
        const Sources sources{
            {"root.ips","import child from \"child.ips\" import other from \"child.ips\" asset t{size 2x1 draw{"+body+"}}"},
            {"child.ips","asset t{size 1x1 output layer image draw{if(seed==1){layer captured{pixel(0,0,#fff)}image=captured}}}"}};
        const auto file=loadFile(compileExports(sources));validate(file);VM vm(file,{});
        CHECK_THROWS_AS(vm.run(),Trap);CHECK_THROWS_AS(vm.result(),std::logic_error);CHECK_THROWS_AS(vm.run(),std::logic_error);
    }
    for(const auto& source:{"asset t{size 1x1 output mask region draw{if(false){layer captured{}local=mask.from(captured)}region=local}}",
        "asset t{size 1x1 output layer image draw{if(false){layer captured{}}image=captured}}"}) {
        const auto file=loadFile(compileExports({{"root.ips",source}}));VM vm(file,{});CHECK_THROWS_AS(vm.run(),Trap);
    }
}

TEST_CASE("resource export validation rejects invalid table fields and dead-code ownership", "[resource-exports]") {
    const Sources sources{
        {"root.ips","import child from \"child.ips\" asset t{size 2x1 output layer image unused(){return;layer captured{}image=captured}draw{child.draw(seed,at={0,0})surface.paint(child.outputs.image)}}"},
        {"child.ips","asset t{size 1x1 output layer image draw{layer captured{}image=captured}}"}};
    const auto bytes=compileExports(sources);const auto file=loadFile(bytes);const auto section=*file.layout.find(SectionId::ResourceExports);
    for(const auto& [offset,value]:{std::pair{0u,65537u},{4u,1000u},{8u,100000u},{28u,100000u}}){
        auto broken=bytes;set32(broken,section.offset+offset,value);CHECK_THROWS_AS(loadFile(broken),FormatError);
    }
    for(const auto& [offset,value]:{std::pair{12u,u16{65535}},{14u,u16{1}}}){
        auto broken=bytes;set16(broken,section.offset+offset,value);CHECK_THROWS_AS(loadFile(broken),FormatError);
    }
    for(const auto opcode:{Opcode::OutLayer,Opcode::PartGetLayer}){
        auto broken=bytes;const auto& fn=file.code.functions[1];
        bool changed=false;
        for(u32 pc=0;pc<fn.code.size()/4;++pc){const auto ins=decodeInstruction(fn.code.subspan(pc*4,4));
            if(ins.opcode==Opcode::OutLayer){broken[fn.fileOffset+pc*4]=static_cast<u8>(opcode);set16(broken,fn.fileOffset+pc*4+2,opcode==Opcode::OutLayer?1:0);changed=true;}}
        REQUIRE(changed);CHECK_THROWS_AS(validate(loadFile(broken)),FormatError);
    }
    auto broken=bytes;set16(broken,6,5);CHECK_THROWS_AS(loadFile(broken),FormatError);
    broken=bytes;set32(broken,section.offset+32,0);CHECK_THROWS_AS(validate(loadFile(broken)),FormatError);
}

TEST_CASE("resource output formatting typing and namespace diagnostics match compilation", "[resource-exports]") {
    const std::string source="asset t{size 2x1 output /* export */mask region output layer image draw{layer captured{pixel(1,0,#fff)}region=mask.from(captured)image=captured}}";
    const auto formatted=ips::formatProgram(ips::parse(source));CHECK(ips::formatProgram(ips::parse(formatted))==formatted);
    CHECK(stripDebug(compileExports({{"root.ips",source}}))==stripDebug(compileExports({{"root.ips",formatted}})));
    for(const auto& bad:{"output list mask region draw{}","output layer image draw{image=1}",
        "output mask region draw{layer image{}region=image}","output layer image output mask image draw{}"}){
        const auto result=compileSource("bad.ips",[&](std::string_view){return "asset t{size 1x1 "+std::string(bad)+"}";});
        CHECK_FALSE(result.succeeded());CHECK(result.bytes.empty());REQUIRE_FALSE(result.diagnostics.empty());CHECK(result.diagnostics[0].location.has_value());
    }
}
