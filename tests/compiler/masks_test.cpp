#include <catch2/catch_test_macros.hpp>
#include "Compiler.hpp"
#include "SourceCompilation.hpp"
#include "iprop/Compression.hpp"
#include "iprop/VM.hpp"
#include "iprop/VmInternals.hpp"
#include "ips/Parser.hpp"
#include "ips/Printer.hpp"
#include "ips/Semantics.hpp"
#include "ipa.hpp"
#include "ipdis.hpp"

#include <array>
#include <bit>
#include <cmath>
#include <map>

using namespace iprop;
namespace {
std::vector<u8> maskProgram(const std::string& source, bool optimize = true) {
    const auto program = ips::parse(source);
    ips::checkAsset(program, {});
    return compileProgram(program, {}, optimize);
}
GenerationResult runMask(const std::vector<u8>& bytes, u32 seed = 26, u32 width = 0, u32 height = 0) {
    const auto file = loadFile(bytes); validate(file);
    VM vm(file, {seed,0,0,width,height,{}}); vm.run(); return vm.result();
}
MaskValue coverage(std::initializer_list<u8> values, u32 width, u32 height) {
    auto buffer = std::make_shared<SurfaceBuffer>();
    buffer->format = SurfaceBuffer::Format::A8;
    buffer->raster.width = width; buffer->raster.height = height;
    buffer->coverage = values;
    return {buffer, {}};
}
void rejectMask(const std::string& source, const std::string& message) {
    const auto result = compileSource("bad-mask.ips", [&](std::string_view) { return source; });
    CHECK_FALSE(result.succeeded()); CHECK(result.bytes.empty());
    REQUIRE_FALSE(result.diagnostics.empty());
    INFO(result.diagnostics.front().message);
    CHECK(result.diagnostics.front().message.find(message) != std::string::npos);
    REQUIRE(result.diagnostics.front().location.has_value());
}
void set16(std::vector<u8>& bytes, std::size_t at, u16 value) {
    bytes.at(at) = static_cast<u8>(value); bytes.at(at + 1) = static_cast<u8>(value >> 8);
}
}

TEST_CASE("mask views share alpha and exact D4 rectangles without allocation", "[masks]") {
    const auto file = loadFile(maskProgram("asset t{size 8x8 draw{}}"));
    detail::Execution ex(file); ex.target = RenderTarget::create(8,8,{1,1}); ex.frames.emplace_back(); ex.fuel = 1000;
    auto layer = RenderTarget::create(2,3,{1,1});
    const std::array<u8,6> original{10,20,30,40,50,60};
    for (std::size_t i=0;i<6;++i) layer.canvas().pixels[i] = {17,34,51,original[i]};
    ex.push(layer.view()); REQUIRE(detail::stepMasks(ex,{Opcode::MaskFrom,0,0}));
    auto mask = std::get<MaskValue>(ex.pop());
    CHECK(mask.buffer == layer.buffer); CHECK(ex.resources.bytes == 0); CHECK(ex.fuel == 1000);
    const std::array<std::array<u8,6>,8> pins{{
        {10,20,30,40,50,60}, {20,40,60,10,30,50}, {60,50,40,30,20,10}, {50,30,10,60,40,20},
        {20,10,40,30,60,50}, {50,60,30,40,10,20}, {10,30,50,20,40,60}, {60,40,20,50,30,10}
    }};
    for (u8 kind=0;kind<8;++kind) {
        mask.mapping = {*D4Transform::fromKind(kind), {-5,7}};
        const auto bounds = maskBounds(mask);
        CHECK(bounds.width == (kind == 1 || kind == 3 || kind >= 6 ? 3u : 2u));
        CHECK(bounds.width * bounds.height == 6);
        for (u32 y=0;y<bounds.height;++y) for (u32 x=0;x<bounds.width;++x)
            CHECK(maskCoverage(mask,-5+static_cast<i32>(x),7+static_cast<i32>(y)) == pins[kind][y*bounds.width+x]);
        CHECK(maskCoverage(mask,-6,7) == 0); CHECK(maskCoverage(mask,-5,6) == 0);
        CHECK(maskCoverage(mask,-5+bounds.width,7) == 0); CHECK(maskCoverage(mask,-5,7+bounds.height) == 0);
        ex.push(mask); ex.push(i32{2}); ex.push(i32{-3});
        REQUIRE(detail::stepMasks(ex,{Opcode::MaskOffset,0,0}));
        const auto moved = std::get<MaskValue>(ex.pop());
        CHECK(moved.buffer == mask.buffer); CHECK(moved.mapping.origin == RasterPoint{-3,4});
        CHECK(ex.resources.bytes == 0); CHECK(ex.fuel == 1000);
    }
}

TEST_CASE("mask algebra retains partial coverage and the union rectangle", "[masks]") {
    const auto file = loadFile(maskProgram("asset t{size 8x8 draw{}}"));
    detail::Execution ex(file); ex.target=RenderTarget::create(8,8,{1,1}); ex.frames.emplace_back(); ex.fuel=1000;
    const auto a = coverage({0,128,255,64},2,2);
    auto b = coverage({255,128,32,192},2,2); b.mapping.origin={1,0};
    const std::array<std::array<u8,6>,3> pins{{{0,255,128,255,64,192},{0,128,0,0,32,0},{0,0,0,255,64,0}}};
    const std::array operations{Opcode::MaskUnion,Opcode::MaskIntersect,Opcode::MaskSubtract};
    for (std::size_t i=0;i<operations.size();++i) {
        ex.push(a); ex.push(b); REQUIRE(detail::stepMasks(ex,{operations[i],0,0}));
        const auto result=std::get<MaskValue>(ex.pop()); const auto bounds=maskBounds(result);
        CHECK(bounds.width==3); CHECK(bounds.height==2); CHECK(result.buffer->raster.pixels.empty());
        CHECK(result.buffer->format==SurfaceBuffer::Format::A8);
        for (std::size_t j=0;j<6;++j) CHECK(result.buffer->coverage[j]==pins[i][j]);
    }
    ex.push(b); REQUIRE(detail::stepMasks(ex,{Opcode::MaskInvert,0,0}));
    const auto inverted=std::get<MaskValue>(ex.pop());
    CHECK(inverted.buffer->coverage==std::vector<u8>{0,127,223,63}); CHECK(inverted.mapping.origin==RasterPoint{1,0});
    CHECK(maskCoverage(inverted,0,0)==0); CHECK(maskCoverage(inverted,3,0)==0);
    CHECK(a.buffer->coverage==std::vector<u8>{0,128,255,64}); CHECK(b.buffer->coverage==std::vector<u8>{255,128,32,192});
    CHECK(ex.resources.bytes==3*134+132); CHECK(ex.fuel==1000-3*12-8);
}

TEST_CASE("mask morphology uses a disk border and normative logical sample fuel", "[masks]") {
    const auto file=loadFile(maskProgram("asset t{size 1x1 draw{}}"));
    detail::Execution ex(file); ex.target=RenderTarget::create(1,1,{1,1}); ex.frames.emplace_back(); ex.fuel=1000000;
    const auto source=coverage({128},1,1);
    ex.push(source);ex.push(i32{1});REQUIRE(detail::stepMasks(ex,{Opcode::MaskExpand,0,0}));
    const auto expanded=std::get<MaskValue>(ex.pop());
    CHECK(expanded.mapping.origin==RasterPoint{-1,-1});
    CHECK(expanded.buffer->coverage==std::vector<u8>{0,128,0,128,128,128,0,128,0});
    CHECK(ex.fuel==1000000-9-9*5);CHECK(ex.resources.bytes==137);
    for (const auto op:{Opcode::MaskExpand,Opcode::MaskShrink}) {
        ex.push(source);ex.push(i32{0});REQUIRE(detail::stepMasks(ex,{op,0,0}));
        CHECK(std::get<MaskValue>(ex.pop()).buffer->coverage==std::vector<u8>{128});
    }
    for (const i32 radius:{1,64}) {
        const auto before=ex.fuel;
        ex.push(source);ex.push(radius);REQUIRE(detail::stepMasks(ex,{Opcode::MaskShrink,0,0}));
        CHECK(std::get<MaskValue>(ex.pop()).buffer->coverage==std::vector<u8>{0});
        CHECK(before-ex.fuel==1+(radius==1?5u:12853u));
    }
    for (const i32 radius:{-1,65}) {
        ex.push(source);ex.push(radius);CHECK_THROWS_AS(detail::stepMasks(ex,{Opcode::MaskExpand,0,0}),Trap);
    }
    const auto allocated=ex.resources.surfaces.size();ex.fuel=53;
    ex.push(source);ex.push(i32{1});CHECK_THROWS_AS(detail::stepMasks(ex,{Opcode::MaskExpand,0,0}),Trap);
    CHECK(ex.resources.surfaces.size()==allocated);
    ex.fuel=1000;ex.resources.bytes=kMaxGenerationData-136;
    ex.push(source);ex.push(i32{1});CHECK_THROWS_AS(detail::stepMasks(ex,{Opcode::MaskExpand,0,0}),Trap);
    CHECK(ex.resources.surfaces.size()==allocated);
}

TEST_CASE("mask use rejects nonfinite mismatched and unrepresentable views before scanning", "[masks]") {
    const auto file=loadFile(maskProgram("asset t{size 1x1 draw{}}"));
    detail::Execution ex(file);ex.target=RenderTarget::create(1,1,{1,1});ex.frames.emplace_back();ex.fuel=1000;
    for(const float scale:{NAN,INFINITY,0.f,-1.f,2.f}){
        auto source=RenderTarget::create(1,1,{scale,1});
        ex.push(MaskValue{source.buffer,{}});CHECK_THROWS_AS(ex.popMask(),Trap);
    }
    auto source=RenderTarget::create(2,1,{1,1});
    ex.push(MaskValue{source.buffer,{{},{2147483647,0}}});CHECK_THROWS_AS(ex.popMask(),Trap);
    source.buffer->format=static_cast<SurfaceBuffer::Format>(255);
    ex.push(MaskValue{source.buffer,{}});CHECK_THROWS_AS(ex.popMask(),Trap);
    source.buffer->format=SurfaceBuffer::Format::A8;
    ex.push(MaskValue{source.buffer,{}});CHECK_THROWS_AS(ex.popMask(),Trap);
    ex.push(source.view());CHECK_THROWS_AS(ex.popLayer(),Trap);
    CHECK(ex.resources.bytes==0);CHECK(ex.fuel==1000);
}

TEST_CASE("clip combines coverage before alpha and zero never changes hidden RGB", "[masks]") {
    const std::string source=R"(asset t{size 6x1 output point state
      mark(x:i32){pixel(x,0,#11223380)}
      echo(region:mask)->mask{return region}
      draw{
        layer coverageImage(alpha=surface.source_over){rect(0,0,3,1,#ffffff80)}
        mask region=echo(mask.from(coverageImage))
        layer image(alpha=surface.source_over){clip(region){mark(0)clip(region){mark(1)}
          surface.paint(image=region,color=#11223380)}}
        surface.paint(image,blend=surface.source_over)
        pixel(4,0,#11223380)clip(region){pixel(4,0,#fff)}
        surface.paint(mask.offset(region,6,0),#fff,blend=surface.source_over)
        state={seed&65535,seed>>16}
      }})";
    for (const bool optimize:{false,true}) {
        const auto bytes=maskProgram(source,optimize);const auto result=runMask(bytes);
        CHECK(result.image.at(0,0)==Color{17,34,51,88});
        CHECK(result.image.at(1,0)==Color{17,34,51,60});
        CHECK(result.image.at(2,0)==Color{17,34,51,32});
        CHECK(result.image.at(4,0)==Color{8,17,25,0});
        CHECK(result.outputs[0].points[0].x==26);CHECK(result.outputs[0].points[0].y==0);
        CHECK(runMask(assemble(disassemble(bytes))).image.pixels==result.image.pixels);
        CHECK(runMask(compressFile(bytes)).image.pixels==result.image.pixels);
        const auto formatted=ips::formatProgram(ips::parse(source));
        CHECK(ips::formatProgram(ips::parse(formatted))==formatted);
        CHECK(runMask(maskProgram(formatted,optimize)).image.pixels==result.image.pixels);
    }
}

TEST_CASE("clip cleanup restores callers through nested capture return break and continue", "[masks]") {
    const std::string source=R"(asset t{size 5x2 output point p
      early(region:mask){clip(region){layer discarded(alpha=surface.legacy){clip(region){return;}}}}
      draw{layer coverageImage(alpha=surface.source_over){rect(0,0,3,2,#ffffff80)}region=mask.from(coverageImage)
        layer image(alpha=surface.source_over){
          early(region)pixel(4,0,#11223380)
          for i in 0..3{clip(region){if(i==0){continue}pixel(i,0,#11223380)if(i==1){break}}}
          for i in 0..2{clip(region){layer captured{pixel(i,1,#ff0000)continue}}}
          surface.paint(captured)p={5,2}pixel(3,0,#11223380)
        }surface.paint(image,blend=surface.source_over)
      }})";
    for (const bool optimize:{false,true}) {
        const auto result=runMask(maskProgram(source,optimize));
        CHECK(result.image.at(0,0)==Color{});CHECK(result.image.at(1,0)==Color{17,34,51,64});
        CHECK(result.image.at(3,0)==Color{17,34,51,128});CHECK(result.image.at(4,0)==Color{17,34,51,128});
        CHECK(result.image.at(0,1)==Color{});CHECK(result.image.at(1,1)==Color{255,0,0,128});
        CHECK(result.outputs[0].points[0].x==5);CHECK(result.outputs[0].points[0].y==2);
    }
}

TEST_CASE("zero mask coverage skips while rounded zero source alpha still composes", "[masks]") {
    const std::string source=R"(asset t{size 3x1 draw{
      layer regionImage(alpha=surface.source_over){pixel(1,0,#ffffff01)pixel(2,0,#ffffff80)}region=mask.from(regionImage)
      rect(0,0,3,1,#10203080)surface.paint(region,color=#2595d501,blend=surface.source_over)
    }})";
    for(const bool optimize:{false,true}){
        const auto result=runMask(maskProgram(source,optimize));
        CHECK(result.image.at(0,0)==Color{8,16,24,0});CHECK(result.image.at(1,0)==Color{});
        CHECK(result.image.at(2,0)==Color{37,149,213,1});
    }
}

TEST_CASE("parent clip applies to the part blit once and geometric outputs stay complete", "[masks]") {
    for (const bool optimize:{false,true}) for (const int rot:{0,90,180,270}) for (const int mirror:{0,1}) {
        const std::map<std::string,std::string> sources{
            {"root.ips","import child from \"child.ips\" asset t{size 8x6 output point p output point state draw{"
             "layer regionImage(alpha=surface.source_over){rect(0,0,8,6,#ffffff80)}region=mask.from(regionImage)"
             "layer image(alpha=surface.source_over){clip(region){child.draw(seed,0,0,at={3,2},rot="+std::to_string(rot)+
             ",mirror_x="+std::to_string(mirror)+")p=child.outputs.p}}surface.paint(image,blend=surface.source_over)state={seed&65535,seed>>16}}}"},
            {"child.ips",R"(asset child{size 3x2 origin 1,0 output point p draw{
              layer regionImage(alpha=surface.source_over){rect(0,0,3,2,#ffffff80)}region=mask.from(regionImage)
              clip(region){pixel(1,0,#11223380)p={100,200}}
            }})"}};
        SourceCompilationOptions options;options.optimize=optimize;
        const auto compiled=compileSource("root.ips",[&](std::string_view path){return sources.at(std::string(path));},options);
        for (const auto& diagnostic:compiled.diagnostics) INFO(diagnostic.message);
        REQUIRE(compiled.succeeded());
        for (const auto size:std::array<RasterPoint,3>{{{8,6},{16,6},{8,12}}}) {
            const auto result=runMask(compiled.bytes,26,size.x,size.y);
            unsigned visible=0;
            for (const auto pixel:result.image.pixels) if(pixel.a){++visible;CHECK(pixel==Color{17,34,51,32});}
            CHECK(visible==1);CHECK(result.outputs[0].points[0].x!=0);CHECK(result.outputs[0].points[0].y!=0);
            CHECK(result.outputs[1].points[0].x==26);CHECK(result.outputs[1].points[0].y==0);
        }
    }
}

TEST_CASE("weather interpolates proposed RGBA by clip coverage without changing stream", "[masks]") {
    const auto full=runMask(maskProgram("asset t{size 4x2 draw{rect(0,0,4,2,#929ca8)weather(0,40)}}"));
    const std::string source=R"(asset t{size 4x2 output point state draw{
      layer regionImage(alpha=surface.source_over){pixel(0,0,#ffffff80)pixel(1,0,#fff)}region=mask.from(regionImage)
      rect(0,0,4,2,#929ca8)clip(region){weather(0,40)}state={seed&65535,seed>>16}}})";
    for (const bool optimize:{false,true}) {
        const auto result=runMask(maskProgram(source,optimize));
        CHECK(full.image.at(0,0)==Color{106,116,128,255});
        CHECK(result.image.at(0,0)==Color{126,136,148,255});CHECK(result.image.at(1,0)==full.image.at(1,0));
        for (u32 y=0;y<2;++y)for(u32 x=0;x<4;++x)if(y||x>=2)CHECK(result.image.at(x,y)==Color{146,156,168,255});
        CHECK(result.outputs[0].points[0].x==26);CHECK(result.outputs[0].points[0].y==0);
    }
}

TEST_CASE("mask paint signature order and clip operand execute once in the caller stream", "[masks]") {
    const std::string source=R"(asset t{size 2x1 output list point trace output point state
      make(alpha:i32)->mask{trace={alpha,seed&65535}unused=1 to 2
        layer image(alpha=surface.source_over){pixel(0,0,rgba(255,255,255,alpha))}return mask.from(image)}
      tone()->color{trace={-1,seed&65535}unused=1 to 2 return #12345680}
      draw{surface.paint(color=tone(),blend=surface.source_over,image=make(128))
        clip(make(64)){unused=1 to 2 pixel(1,0,#fff)}state={seed&65535,seed>>16}}
    })";
    for(const bool optimize:{false,true}){
        const auto result=runMask(maskProgram(source,optimize));
        REQUIRE(result.outputs[0].points.size()==3);
        CHECK(result.outputs[0].points[0].x==128);CHECK(result.outputs[0].points[0].y==26);
        CHECK(result.outputs[0].points[1].x==-1);CHECK(result.outputs[0].points[1].y==31187);
        CHECK(result.outputs[0].points[2].x==64);CHECK(result.outputs[0].points[2].y==62348);
        CHECK(result.outputs[1].points[0].x==59134);CHECK(result.outputs[1].points[0].y==30941);
        CHECK(result.image.at(0,0)==Color{18,52,86,64});CHECK(result.image.at(1,0)==Color{});
        const auto formatted=ips::formatProgram(ips::parse(source));
        const auto after=runMask(maskProgram(formatted,optimize));
        CHECK(after.image.pixels==result.image.pixels);
        REQUIRE(after.outputs[0].points.size()==3);
        for(std::size_t i=0;i<3;++i)CHECK(after.outputs[0].points[i].y==result.outputs[0].points[i].y);
    }
}

TEST_CASE("mask source types diagnostics and format gates reject invalid uses", "[masks]") {
    rejectMask("asset t{size 1x1 const i32 invalidRadius=65 draw{layer image{}region=mask.from(image)"
        "wide=mask.expand(region,invalidRadius)}}", "mask radius must be in 0..64");
    for (const auto& [body,message]:std::map<std::string,std::string>{
        {"clip(1){}","clip requires mask"}, {"m=mask.from(1)","must be layer"},
        {"m=mask.expand(region,65)","mask radius must be in 0..64"},
        {"m=mask.shrink(region,-1)","mask radius must be in 0..64"},
        {"m=mask.offset(region,1.5,0)","must be i32"},
        {"surface.paint(region)","expects 2 or 3 arguments"},
        {"surface.paint(region,#fff,blend=1)","blend must be surface.legacy"},
        {"m=region+region","arithmetic requires numeric"}, {"m=region==region","comparison"}})
        rejectMask("asset t{size 1x1 draw{layer image{}region=mask.from(image)"+body+"}}",message);
    for (const auto& source:{"asset t{size 1x1 input mask image draw{}}", "asset t{size 1x1 output mask image draw{}}",
            "asset t{size 1x1 const mask image=1 draw{}}", "asset t{size 1x1 echo(images:list<mask>){}draw{}}"}) {
        const auto result=compileSource("bad.ips",[&](std::string_view){return source;});
        CHECK_FALSE(result.succeeded());CHECK(result.bytes.empty());
    }
    const auto valid=maskProgram("asset t{size 1x1 draw{layer image{}region=mask.from(image)clip(region){}surface.paint(region,#fff,blend=surface.source_over)}}",false);
    CHECK(loadFile(valid).layout.header.versionMinor==6);
    const auto file=loadFile(valid);
    for(const auto& fn:file.code.functions)for(std::size_t pc=0;pc<fn.code.size()/4;++pc){
        const auto ins=decodeInstruction(fn.code.subspan(pc*4,4));
        if(ins.opcode==Opcode::SurfacePaintMask)for(const u16 mode:{u16{3},u16{257},u16{65535}}){
            auto bad=valid;set16(bad,static_cast<std::size_t>(fn.code.data()-file.bytes().data())+pc*4+2,mode);
            CHECK_THROWS_AS(validate(loadFile(bad)),FormatError);
        }
    }
    for (const u16 minor:{u16{1},u16{2},u16{3},u16{4},u16{5}}) {
        auto bad=valid;set16(bad,6,minor);CHECK_THROWS_AS(loadFile(bad),FormatError);
    }
}

TEST_CASE("combined surface scopes unset masks and failure release generation resources", "[masks]") {
    for (const auto& body:{"if(v==1){layer image{}region=mask.from(image)}clip(region){}",
            "layer image{}region=mask.from(image)x=mask.expand(region,v+65)",
            "layer image{}region=mask.from(image)x=mask.expand(region,64)"}) {
        const auto file=loadFile(maskProgram("asset t{size 1x1 draw{"+std::string(body)+"}}",false));
        VM vm(file,{26,0,0,0,0,{}});CHECK_THROWS_AS(vm.run(),Trap);
        CHECK_THROWS_AS(vm.result(),std::logic_error);CHECK(vm.canvas().width==1);
    }
    std::string scopes="layer image{}region=mask.from(image)";
    for (int i=0;i<32;++i) scopes+="clip(region){";
    scopes+=std::string(32,'}');CHECK_NOTHROW(maskProgram("asset t{size 1x1 draw{"+scopes+"}}",false));
    scopes="layer image{}region=mask.from(image)";
    for (int i=0;i<16;++i) scopes+="clip(region){layer nested"+std::to_string(i)+"{";
    scopes+="clip(region){}"+std::string(32,'}');
    CHECK_THROWS_AS(maskProgram("asset t{size 1x1 draw{"+scopes+"}}",false),ips::SourceError);
}
