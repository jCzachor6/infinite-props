#include <catch2/catch_test_macros.hpp>
#include "Compiler.hpp"
#include "SourceCompilation.hpp"
#include "iprop/Compression.hpp"
#include "iprop/Debug.hpp"
#include "iprop/VmInternals.hpp"
#include "iprop/VM.hpp"
#include "ips/Parser.hpp"
#include "ips/Printer.hpp"
#include "ips/Semantics.hpp"
#include "ipa.hpp"
#include "ipdis.hpp"

#include <algorithm>
#include <bit>
#include <map>
#include <string>

using namespace iprop;
namespace {
std::vector<u8> surfaceProgram(const std::string& text, bool optimize = true) {
    const auto p = ips::parse(text); ips::checkAsset(p, {}); return compileProgram(p, {}, optimize);
}
GenerationResult surfaceResult(const std::vector<u8>& bytes, u32 seed = 26, u32 width = 0, u32 height = 0) {
    const auto file = loadFile(bytes); validate(file); VM vm(file, {seed,0,0,width,height,{}}); vm.run(); return vm.result();
}
void sameOutputs(const GenerationResult& a, const GenerationResult& b) {
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
void rejectSurface(const std::string& text, const std::string& message) {
    try { (void)surfaceProgram(text); FAIL("invalid surface source accepted"); }
    catch (const ips::SourceError& error) {
        INFO(error.what()); CHECK(std::string(error.what()).find(message) != std::string::npos);
        CHECK(error.location.line > 0); CHECK(error.location.column > 0);
    }
}
using Sources = std::map<std::string, std::string>;
std::vector<u8> surfaceTree(const Sources& sources, bool optimize) {
    SourceCompilationOptions options; options.optimize = optimize;
    const auto result = compileSource("root.ips", [&](std::string_view path) { return sources.at(std::string(path)); }, options);
    for (const auto& diagnostic : result.diagnostics) INFO(diagnostic.message);
    REQUIRE(result.succeeded()); return result.bytes;
}
void raw16(std::vector<u8>& bytes, std::size_t at, u16 value) {
    bytes.at(at) = static_cast<u8>(value); bytes.at(at+1) = static_cast<u8>(value >> 8);
}
}

TEST_CASE("layers capture once and do not automatically paint the parent", "[surfaces]") {
    const std::string declarations = "asset t{size 8x6 output point p output point state ";
    const auto reference = surfaceProgram(declarations + "draw{w=2 to 6 rect(1,1,w,3,#929ca8) p={w,w} state={seed&65535,seed>>16}}}", false);
    for (const bool optimize : {false,true}) {
        const auto bytes = surfaceProgram(declarations +
            "echo(image:layer)->layer{return image} draw{layer image(alpha=surface.legacy){w=2 to 6 rect(1,1,w,3,#929ca8) p={w,w}} "
            "copy=echo(image);surface.paint(copy,blend=surface.legacy);surface.paint(image) state={seed&65535,seed>>16}}}", optimize);
        for (const u32 seed : {0u,1u,7u,26u,0xffffffffu}) {
            const auto actual = surfaceResult(bytes,seed), expected = surfaceResult(reference,seed);
            CHECK(actual.image.pixels == expected.image.pixels); sameOutputs(actual,expected);
            const u32 state = seed + 0x9e3779b9u;
            CHECK(actual.outputs[1].points[0].x == static_cast<f32>(state & 65535));
            CHECK(actual.outputs[1].points[0].y == static_cast<f32>(state >> 16));
        }
        const auto hidden = surfaceResult(surfaceProgram("asset t{size 2x2 draw{layer image{pixel(0,0,#ff0000)} pixel(1,1,#0000ff)}}",optimize));
        CHECK(hidden.image.at(0,0) == Color{}); CHECK(hidden.image.at(1,1) == Color{0,0,255,255});
    }
}

TEST_CASE("layer snapshots survive parent writes and later captures", "[surfaces]") {
    const std::string source = R"(asset t {size 3x2
      echo(image:layer)->layer{return image}
      make()->layer{layer first{pixel(0,0,#929ca8)} return first}
      draw{image=make() copy=echo(image) pixel(0,0,#ffffff)
        layer image{pixel(1,0,#343a42)} surface.paint(image) surface.paint(copy)
        layer nested{layer inner{pixel(2,0,#c09060)} surface.paint(inner)} surface.paint(nested)
        weather(7,20) surface.paint(copy)}
    })";
    for (const bool optimize : {false,true}) {
        const auto result = surfaceResult(surfaceProgram(source,optimize));
        CHECK(result.image.at(0,0) == Color{146,156,168,255});
        CHECK(result.image.at(1,0) == Color{52,58,66,255});
        CHECK(result.image.at(2,0) == Color{192,144,96,255});
        CHECK(result.image.at(0,1) == Color{});
    }
}

TEST_CASE("surface compositing preserves legacy hidden RGB and transparent skips", "[surfaces]") {
    for (const bool optimize : {false,true}) {
        const auto result = surfaceResult(surfaceProgram(R"(asset t{size 4x1 draw{
          pixel(0,0,#11223380) pixel(1,0,#112233) pixel(2,0,#112233)
          layer image{pixel(0,0,#ff000080) pixel(1,0,#0000ff00)
            pixel(2,0,#929ca8) pixel(2,0,#ff000080)}
          surface.paint(image)
        }})",optimize));
        CHECK(result.image.at(0,0) == Color{8,17,25,0});
        CHECK(result.image.at(1,0) == Color{17,34,51,255});
        CHECK(result.image.at(2,0) == Color{200,77,83,255});
        CHECK(result.image.at(3,0) == Color{});
    }
}

TEST_CASE("return break and continue freeze captures and restore every caller target", "[surfaces]") {
    const std::string source = R"(asset t{size 5x2 output point state
      abandon(){layer forgotten{pixel(0,1,#ffffff) return;} pixel(1,1,#ffffff)}
      chooseImage()->layer{layer earlier{pixel(4,0,#ffffff)} layer discarded{pixel(3,1,#ffffff) return earlier;} return discarded}
      draw{
        for i in 0..3{layer image{pixel(i,0,#ff0000) if(i==0){continue} if(i==1){break}}}
        surface.paint(image) abandon() pixel(3,0,#0000ff) surface.paint(chooseImage())
        for j in 0..2{layer outer{pixel(j,1,#00ff00) layer inner{pixel(2,1,#ffffff) continue}}}
        surface.paint(outer) surface.paint(inner) state={seed&65535,seed>>16}
      }})";
    for (const bool optimize : {false,true}) {
        const auto r = surfaceResult(surfaceProgram(source,optimize));
        CHECK(r.image.at(0,0) == Color{}); CHECK(r.image.at(1,0) == Color{255,0,0,255});
        CHECK(r.image.at(2,0) == Color{}); CHECK(r.image.at(3,0) == Color{0,0,255,255});
        CHECK(r.image.at(4,0) == Color{255,255,255,255});
        CHECK(r.image.at(0,1) == Color{}); CHECK(r.image.at(1,1) == Color{0,255,0,255});
        CHECK(r.image.at(2,1) == Color{255,255,255,255}); CHECK(r.image.at(3,1) == Color{});
        CHECK(r.outputs[0].points[0].x == 26.f); CHECK(r.outputs[0].points[0].y == 0.f);
    }
}

TEST_CASE("captured part trees retain D4 placement scale outputs and isolated RNG", "[surfaces]") {
    for (const int rot : {0,90,180,270}) for (const int mirror : {0,1}) {
        const std::string call = "child.draw(seed,0,0,at={7.5,6.5},rot="+std::to_string(rot)+",mirror_x="+std::to_string(mirror)+")";
        const std::string header = "import child from \"child.ips\" asset t{size 16x12 output point p output point state ";
        Sources reference{{"root.ips",header+"draw{"+call+" p=child.outputs.p state={seed&65535,seed>>16}}}"},
            {"child.ips",R"(import leaf from "leaf.ips" asset child{size 7x5 origin 2,1 output point p draw{
              leaf.draw(seed,0,0,at={3,2},rot=90,mirror_y=1) p=leaf.outputs.p pixel(6,4,#00ff00)
            }})"},
            {"leaf.ips",R"(asset leaf{size 3x2 origin 1,0 output point p draw{pixel(0,0,#929ca8) pixel(2,1,#c09060) p={1.0 to 2.0,0}}})"}};
        Sources captured = reference;
        captured["root.ips"] = header + "draw{layer image{"+call+" p=child.outputs.p} surface.paint(image) state={seed&65535,seed>>16}}}";
        captured["child.ips"] = R"(import leaf from "leaf.ips" asset child{size 7x5 origin 2,1 output point p draw{
          layer image{leaf.draw(seed,0,0,at={3,2},rot=90,mirror_y=1) p=leaf.outputs.p pixel(6,4,#00ff00)} surface.paint(image)
        }})";
        for (const bool optimize : {false,true}) {
            const auto a = surfaceTree(captured,optimize), b = surfaceTree(reference,optimize);
            for (const auto size : std::vector<RasterPoint>{{16,12},{32,12},{8,24}}) for (const u32 seed : {0u,26u,0xffffffffu}) {
                const auto actual = surfaceResult(a,seed,size.x,size.y), expected = surfaceResult(b,seed,size.x,size.y);
                CHECK(actual.image.pixels == expected.image.pixels); sameOutputs(actual,expected);
                CHECK(actual.outputs[1].points[0].x == static_cast<f32>(seed & 65535));
            }
        }
    }
}

TEST_CASE("surface buffers share storage and quota and fuel preflight precede work", "[surfaces]") {
    const auto bytes = surfaceProgram("asset t{size 2x2 echo(image:layer)->layer{return image} draw{layer image{}surface.paint(image)}}",false);
    auto file = loadFile(bytes);
    detail::Execution ex(file); ex.target=RenderTarget::create(2,2,{0.5f,2}); ex.fuel=100;
    detail::Frame root;root.locals={i32{0},i32{0},LayerValue{}};root.stream=26;ex.frames.push_back(std::move(root));
    const auto original=ex.target.buffer;
    ex.beginLayer(2,0);
    const auto buffer=ex.target.buffer;
    CHECK(buffer!=original);CHECK(ex.resources.bytes==144);CHECK(ex.fuel==96);
    CHECK(ex.canvas().scaleX==0.5f);CHECK(ex.canvas().scaleY==2.f);
    detail::compositePixel(ex,1,1,{146,156,168,255});ex.closeLayers(0);
    const auto image=std::get<LayerValue>(ex.frame().locals[2]);
    CHECK(image.buffer.get()==buffer.get());CHECK(ex.target.buffer==original);CHECK(ex.layerScopes.empty());
    ex.push(image);ex.callFunction(1);
    CHECK(std::get<LayerValue>(ex.frame().locals[0]).buffer==image.buffer);
    ex.push(ex.frame().locals[0]);ex.leaveFrame();
    CHECK(std::get<LayerValue>(ex.peek()).buffer==image.buffer);
    CHECK(detail::stepSurfaces(ex,{Opcode::SurfacePaint,0,0}));
    CHECK(ex.canvas().at(1,1)==Color{146,156,168,255});CHECK(ex.resources.bytes==144);CHECK(ex.fuel==92);CHECK(ex.frame().stream==26);
    auto view=image;view.mapping={*D4Transform::fromKind(1),{1,0}};
    CHECK(view.buffer==image.buffer);CHECK(ex.resources.bytes==144);
    detail::Execution limited(file);limited.target=RenderTarget::create(2,2,{1,1});limited.frames.push_back({});
    limited.frame().locals={i32{0},i32{0},LayerValue{}};limited.fuel=3;
    CHECK_THROWS_AS(limited.beginLayer(2,0),Trap);CHECK(limited.resources.bytes==0);CHECK(limited.layerScopes.empty());
    limited.fuel=100;limited.resources.bytes=kMaxGenerationData-143;
    CHECK_THROWS_AS(limited.beginLayer(2,0),Trap);CHECK(limited.resources.surfaces.empty());CHECK(limited.layerScopes.empty());
    limited.push(image);limited.fuel=3;
    CHECK_THROWS_AS(detail::stepSurfaces(limited,{Opcode::SurfacePaint,0,0}),Trap);
    CHECK(std::all_of(limited.canvas().pixels.begin(),limited.canvas().pixels.end(),[](Color c){return c==Color{};}));
    limited.fuel=100;limited.push(LayerValue{});
    CHECK_THROWS_AS(detail::stepSurfaces(limited,{Opcode::SurfacePaint,0,0}),Trap);CHECK(limited.fuel==100);
}

TEST_CASE("surface scope and shared arena limits fail without publishing an image", "[surfaces]") {
    std::string nested = "asset t{size 1x1 draw{";
    for(int i=0;i<32;++i)nested += "layer image"+std::to_string(i)+"{";
    nested += "pixel(0,0,#fff)";
    for(int i=31;i>=0;--i)nested += "}surface.paint(image"+std::to_string(i)+")";
    nested += "}}";
    CHECK(surfaceResult(surfaceProgram(nested)).image.at(0,0)==Color{255,255,255,255});
    const auto innerLoop=surfaceResult(surfaceProgram("asset t{size 2x1 draw{layer image{while(true){pixel(0,0,#fff)break;}pixel(1,0,#929ca8)}surface.paint(image)}}"));
    CHECK(innerLoop.image.at(0,0)==Color{255,255,255,255});CHECK(innerLoop.image.at(1,0)==Color{146,156,168,255});
    const auto recursive=assemble("meta: size=1x1\ntypes: layer\nfn draw ( i32, i32 ) -> ( ):\ncall nested\nreturn\nendfn\n"
        "fn nested ( ) -> ( ):\nlocals: layer\nlayer.begin 0\ncall nested\nend\nreturn\nendfn\n");
    const auto file=loadFile(recursive);validate(file);VM vm(file,{});
    try{vm.run();FAIL("33rd surface scope accepted");}catch(const Trap& error){CHECK(std::string(error.what())=="surface scope depth exceeds 32");}
    CHECK_THROWS_AS(vm.result(),std::logic_error);CHECK(vm.canvas().at(0,0)==Color{});CHECK_THROWS_AS(vm.run(),std::logic_error);
    std::string assembly="meta: size=1024x1024 fuel=100000000\ntypes: layer\nfn draw ( i32, i32 ) -> ( ):\nlocals: layer\n";
    for(int i=0;i<16;++i)assembly+="layer.begin 2\n";
    for(int i=0;i<16;++i)assembly+="end\n";
    assembly+="return\nendfn\n";
    const auto oversized=loadFile(assemble(assembly));VM quota(oversized,{});
    try{quota.run();FAIL("surface arena exceeded");}catch(const Trap& error){CHECK(error.reason==ErrorReason::OutOfMemory);}
    CHECK_THROWS_AS(quota.takeResult(),std::logic_error);
    const auto shared=surfaceProgram("asset t{size 1024x1024 echo(image:layer)->layer{return image} draw{layer image{} for i in 0..10000{copy=echo(image)}}}",false);
    CHECK_NOTHROW(surfaceResult(shared));
}

TEST_CASE("layer descriptors scopes and modes survive tools and reject malformed wire data", "[surfaces]") {
    const auto bytes=surfaceProgram("asset t{size 2x2 draw{layer image(alpha=surface.legacy){pixel(1,1,#fff)}surface.paint(image,blend=surface.legacy)}}",false);
    const auto file=loadFile(bytes);REQUIRE(file.layout.header.versionMinor==6);
    REQUIRE(file.collections.types==std::vector<ValueType>{ValueType::Layer});
    const auto restored=assemble(disassemble(bytes));CHECK(surfaceResult(restored).image.pixels==surfaceResult(bytes).image.pixels);
    CHECK(disassemble(restored).find("layer.begin 2 1")!=std::string::npos);
    CHECK(stripDebug(decompressFile(compressFile(bytes)))==stripDebug(bytes));
    const auto descriptor=file.layout.find(SectionId::Types)->offset+4;
    for(const auto [relative,value]:std::vector<std::pair<std::size_t,u8>>{{0,255},{1,0},{2,1},{3,1}}){
        auto bad=bytes;bad.at(descriptor+relative)=value;CHECK_THROWS_AS(loadFile(bad),FormatError);
    }
    for(std::size_t i=0;i<file.code.functions[0].code.size()/4;++i){
        const auto at=file.code.functions[0].fileOffset+i*4;const auto ins=decodeInstruction(file.code.functions[0].code.subspan(i*4,4));
        if(ins.opcode==Opcode::LayerBegin){
            for(const u16 destination:{0u,1u,65535u}){auto bad=bytes;raw16(bad,at+2,destination);CHECK_THROWS_AS(validate(loadFile(bad)),FormatError);}
            auto bad=bytes;bad.at(at+1)=3;CHECK_THROWS_AS(validate(loadFile(bad)),FormatError);
        }
        if(ins.opcode==Opcode::SurfacePaint){
            auto bad=bytes;raw16(bad,at+2,3);CHECK_THROWS_AS(validate(loadFile(bad)),FormatError);
            bad=bytes;bad.at(at+1)=1;CHECK_THROWS_AS(validate(loadFile(bad)),FormatError);
        }
    }
    auto old=bytes;raw16(old,6,5);CHECK_THROWS_AS(loadFile(old),FormatError);
    for(const std::string dead:{"layer.begin 0 3\nend\n", "layer.begin 65535\nend\n", "surface.paint 3\n"})
        CHECK_THROWS_AS(assemble("meta: size=1x1\nfn draw ( i32, i32 ) -> ( ):\nreturn\n"+dead+"endfn\n"),std::exception);
}

TEST_CASE("layer formatting and illegal handle operations have located diagnostics", "[surfaces]") {
    const std::string source="asset t{size 2x2 input u32 copies=1 count(x:i32)->i32{return x} "
        "draw{for i in 0..copies{layer image(alpha=/* mode */surface.legacy){pixel(0,0,#fff)}} "
        "layer copy=image;surface.paint(blend=surface.legacy,image=copy);x=count(2)}}";
    const auto formatted=ips::formatProgram(ips::parse(source));CHECK(formatted.find("/* mode */")!=std::string::npos);
    CHECK(ips::formatProgram(ips::parse(formatted))==formatted);CHECK(stripDebug(surfaceProgram(source))==stripDebug(surfaceProgram(formatted)));
    for(const auto& [body,message]:std::initializer_list<std::pair<const char*,const char*>>{
        {"layer image{surface.paint(image)}","undefined"}, {"image=1;layer image{}","conflicts"},
        {"layer image{}x=image+image","numeric"}, {"layer image{}x=image==image","numeric"},
        {"layer image{}x=image[0]","list"}, {"layer image{}x=image.r","needs a color"},
        {"layer image{}surface.paint(image,blend=1)","surface.legacy"},
        {"layer image(alpha=surface.unknown){}","surface.source_over"}})
        rejectSurface("asset t{size 1x1 draw{"+std::string(body)+"}}",message);
    rejectSurface("asset t{size 1x1 const layer image=1 draw{}}","cannot be const");
    rejectSurface("asset t{size 1x1 f(images:list<layer>){}draw{}}","lists of surfaces");
    rejectSurface("asset t{size 1x1 f(images:sampler<layer>){}draw{}}","supports only");
    rejectSurface("asset t{size 1x1 input layer image draw{}}","input type");
    rejectSurface("asset t{size 1x1 output layer image draw{}}","output kind");
    for(const bool optimize:{false,true}) {
        const auto unset=loadFile(surfaceProgram("asset t{size 1x1 draw{if(false){layer image{}}surface.paint(image)}}",optimize));VM vm(unset,{});
        try{vm.run();FAIL("unset layer accepted");}catch(const Trap& error){CHECK(std::string(error.what())=="layer is unassigned");CHECK((error.location.flags & ErrorLocation::PC) != 0);}
    }
}
