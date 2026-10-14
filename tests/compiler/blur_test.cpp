#include <catch2/catch_test_macros.hpp>
#include "Compiler.hpp"
#include "SourceCompilation.hpp"
#include "iprop/Compression.hpp"
#include "iprop/Debug.hpp"
#include "iprop/VmInternals.hpp"
#include "iprop/VM.hpp"
#include "ips/BuiltinSpec.hpp"
#include "ips/Parser.hpp"
#include "ips/Printer.hpp"
#include "ips/Semantics.hpp"
#include "ipa.hpp"
#include "ipdis.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <string>

using namespace iprop;
namespace {
struct BlurExecution {
    LoadedFile file;
    detail::Execution ex{file};
    BlurExecution() {
        file.code.functions.emplace_back();
        ex.frames.emplace_back(); ex.frame().stream = 26;
        ex.target = RenderTarget::create(3, 2, {7, 9});
        ex.budget = ex.fuel = 1000000000;
    }
    template<class Handle> Handle run(Handle input, f32 sigma) {
        ex.push(std::move(input)); ex.push(sigma);
        REQUIRE(detail::stepBlur(ex, {std::is_same_v<Handle, LayerValue> ? Opcode::LayerBlur : Opcode::MaskBlur, 0, 0}));
        return std::get<Handle>(ex.pop());
    }
};
std::shared_ptr<SurfaceBuffer> imageBuffer(u32 w, u32 h) {
    auto buffer = std::make_shared<SurfaceBuffer>();
    buffer->raster.resize(w, h); buffer->raster.scaleX = 0.5f; buffer->raster.scaleY = 2.0f;
    return buffer;
}
u8 byte(long double value) {
    return static_cast<u8>(std::floor(std::clamp(value, 0.0L, 255.0L) + 0.5L));
}
// Independent full 2D convolution: long-double libm weights, no pinnedExp,
// separable intermediate, ring cache or runtime pixel conversion helpers.
std::vector<Color> reference(const Canvas& source, f32 sigma) {
    const int radius = static_cast<int>(std::ceil(3.0L * sigma));
    std::vector<long double> weights;
    long double normalization = 0;
    for (int j = -radius; j <= radius; ++j) {
        const long double weight = std::exp(-static_cast<long double>(j*j) / (2.0L*sigma*sigma));
        weights.push_back(weight); normalization += weight;
    }
    for (auto& weight : weights) weight /= normalization;
    std::vector<Color> result;
    for (u32 y = 0; y < source.height; ++y) for (u32 x = 0; x < source.width; ++x) {
        std::array<long double, 4> sum{};
        for (int dy = -radius; dy <= radius; ++dy) for (int dx = -radius; dx <= radius; ++dx) {
            const auto sx = std::clamp(static_cast<i64>(x)+dx, i64{0}, static_cast<i64>(source.width)-1);
            const auto sy = std::clamp(static_cast<i64>(y)+dy, i64{0}, static_cast<i64>(source.height)-1);
            const auto c = source.at(static_cast<u32>(sx), static_cast<u32>(sy));
            const auto w = weights[static_cast<std::size_t>(dy+radius)] * weights[static_cast<std::size_t>(dx+radius)];
            sum[0] += c.r * static_cast<long double>(c.a) * w;
            sum[1] += c.g * static_cast<long double>(c.a) * w;
            sum[2] += c.b * static_cast<long double>(c.a) * w;
            sum[3] += c.a * w;
        }
        const auto a = byte(sum[3]);
        result.push_back(a ? Color{byte(sum[0]/sum[3]), byte(sum[1]/sum[3]), byte(sum[2]/sum[3]), a} : Color{});
    }
    return result;
}
std::vector<u8> compileBlur(const std::string& source, bool optimize = true) {
    const auto program = ips::parse(source); ips::checkAsset(program, {});
    return compileProgram(program, {}, optimize);
}
GenerationResult generateBlur(const std::vector<u8>& bytes, u32 seed = 26, u32 w = 0, u32 h = 0) {
    const auto file = loadFile(bytes); validate(file); VM vm(file, {seed,w,h,{}}); vm.run(); return vm.result();
}
void reject(const std::string& source, const std::string& expected) {
    try { (void)compileBlur(source); FAIL("invalid blur source accepted"); }
    catch (const ips::SourceError& error) {
        INFO(error.what()); CHECK(std::string(error.what()).find(expected) != std::string::npos);
        CHECK(error.location.line > 0); CHECK(error.location.column > 0);
    }
}
}

TEST_CASE("Gaussian blur matches independent 2D convolution and mask alpha", "[blur]") {
    for (const auto size : std::array<RasterPoint, 6>{{{1,1},{1,9},{11,1},{3,7},{8,5},{5,19}}})
    for (const f32 sigma : {0.25f, 0.75f, 1.0f, 2.0f, 64.0f}) {
        // Keep the maximum-radius full 2D oracle small; the ring is tested
        // beyond its wrap point at sigma <= 2 on the 19-row raster.
        if (sigma == 64 && size.x * size.y > 21) continue;
        CAPTURE(size.x, size.y, sigma);
        auto source = imageBuffer(size.x, size.y);
        for (u32 y = 0; y < source->raster.height; ++y) for (u32 x = 0; x < source->raster.width; ++x)
            source->raster.at(x,y) = {static_cast<u8>(17+x*37+y*11), static_cast<u8>(x*83+y*57),
                static_cast<u8>(255-x*19-y*3), static_cast<u8>((x*71+y*53+128)%256)};
        const auto original = source->raster.pixels;
        const auto expected = reference(source->raster, sigma);
        BlurExecution fixture;
        const auto image = fixture.run(LayerValue{source,{}}, sigma);
        CHECK(image.buffer->raster.pixels == expected);
        const auto mask = fixture.run(MaskValue{source,{}}, sigma);
        REQUIRE(mask.buffer->format == SurfaceBuffer::Format::A8);
        REQUIRE(mask.buffer->coverage.size() == expected.size());
        auto a8 = imageBuffer(size.x, size.y); a8->format = SurfaceBuffer::Format::A8; a8->raster.pixels.clear();
        for (const auto c : original) a8->coverage.push_back(c.a);
        const auto coverage = fixture.run(MaskValue{a8,{}}, sigma);
        for (std::size_t i = 0; i < expected.size(); ++i) CHECK(mask.buffer->coverage[i] == image.buffer->raster.pixels[i].a);
        CHECK(mask.buffer->coverage == coverage.buffer->coverage);
        CHECK(source->raster.pixels == original); CHECK(fixture.ex.frame().stream == 26);
        CHECK(std::all_of(fixture.ex.canvas().pixels.begin(), fixture.ex.canvas().pixels.end(), [](Color c){return c == Color{};}));
    }
}

TEST_CASE("Gaussian blur impulse edge coverage constants and premultiplied alpha have pinned results", "[blur]") {
    BlurExecution fixture;
    auto impulse = imageBuffer(7,1); impulse->raster.at(3,0) = {255,80,40,255};
    const auto blurred = fixture.run(LayerValue{impulse,{}},1);
    const std::array<u8,7> expected{1,14,62,102,62,14,1};
    for (u32 x = 0; x < 7; ++x) CHECK(blurred.buffer->raster.at(x,0) == Color{255,80,40,expected[x]});
    auto step = imageBuffer(7,1);
    for (u32 x = 3; x < 7; ++x) step->raster.at(x,0) = {255,255,255,255};
    const auto soft = fixture.run(MaskValue{step,{}},1);
    CHECK(soft.buffer->coverage == std::vector<u8>{1,15,77,178,240,254,255});
    // A corner impulse distinguishes nearest-edge extension from zero padding.
    auto corner = imageBuffer(3,3); corner->raster.at(0,0) = {90,180,240,255};
    const auto edge = fixture.run(MaskValue{corner,{}},1);
    CHECK(edge.buffer->coverage == std::vector<u8>{125,54,10,54,23,4,10,4,1});
    for (const auto color : {Color{23,117,219,255}, Color{23,117,219,128}, Color{99,88,77,0}})
    for (const f32 sigma : {0.01f, 0.5f, 2.f, 64.f}) {
        auto flat = imageBuffer(3,2); std::fill(flat->raster.pixels.begin(),flat->raster.pixels.end(),color);
        const auto result = fixture.run(LayerValue{flat,{}},sigma);
        const auto wanted = color.a ? color : Color{};
        CHECK(std::all_of(result.buffer->raster.pixels.begin(),result.buffer->raster.pixels.end(),[&](Color c){return c == wanted;}));
    }
    auto hidden = imageBuffer(3,1); hidden->raster.pixels = {{0,255,0,0},{255,0,0,128},{0,0,255,0}};
    const auto red = fixture.run(LayerValue{hidden,{}},1);
    for (const auto pixel : red.buffer->raster.pixels) { CHECK(pixel.r == 255); CHECK(pixel.g == 0); CHECK(pixel.b == 0); }
    auto tiny = imageBuffer(7,1); tiny->raster.at(3,0)={255,200,100,1};
    const auto transparent = fixture.run(LayerValue{tiny,{}},1);
    CHECK(std::all_of(transparent.buffer->raster.pixels.begin(),transparent.buffer->raster.pixels.end(),[](Color c){return c == Color{};}));
}

TEST_CASE("blur keeps raster geometry mapping and zero sigma shares even hidden pixels", "[blur]") {
    auto source = imageBuffer(3,7); source->raster.at(1,2) = {200,40,80,0}; source->raster.at(2,4) = {10,120,210,128};
    for (u8 kind = 0; kind < 8; ++kind) for (const f32 sigma : {0.f,-0.f,0.5f,2.f}) {
        BlurExecution fixture;
        const SurfaceMapping mapping{*D4Transform::fromKind(kind), {-11,17}};
        const auto layer = fixture.run(LayerValue{source,mapping},sigma);
        const auto mask = fixture.run(MaskValue{source,mapping},sigma);
        for (const auto& buffer : {layer.buffer,mask.buffer}) {
            CHECK(buffer->raster.width == 3); CHECK(buffer->raster.height == 7);
            CHECK(buffer->raster.scaleX == 0.5f); CHECK(buffer->raster.scaleY == 2.0f);
        }
        CHECK(layer.mapping.origin == mapping.origin); CHECK(layer.mapping.transform == mapping.transform);
        CHECK(mask.mapping.origin == mapping.origin); CHECK(mask.mapping.transform == mapping.transform);
        if (sigma == 0) {
            CHECK(layer.buffer == source); CHECK(mask.buffer == source);
            CHECK(layer.buffer->raster.at(1,2) == Color{200,40,80,0});
            CHECK(fixture.ex.resources.bytes == 0); CHECK(fixture.ex.resources.peak == 0);
        } else { CHECK(layer.buffer != source); CHECK(mask.buffer != source); }
    }
}

TEST_CASE("blur preflights exact fuel and shared arena and releases its row cache", "[blur]") {
    for (const bool mask : {false,true}) {
        auto source = imageBuffer(5,19);
        BlurExecution fixture;
        const auto sigma = 1.0f;
        const u64 resultBytes = mask ? maskBytes(5,19) : surfaceBytes(5,19);
        const u64 cacheBytes = 5u*7u*(mask ? 1u : 4u)*sizeof(double);
        const u64 cost = 7u+5u*19u*15u;
        const auto before = fixture.ex.fuel;
        Value result = mask ? Value(fixture.run(MaskValue{source,{}},sigma)) : Value(fixture.run(LayerValue{source,{}},sigma));
        CHECK(fixture.ex.fuel == before-cost);
        CHECK(fixture.ex.resources.bytes == resultBytes);
        CHECK(fixture.ex.resources.peak == resultBytes+cacheBytes);
        result = i32{0}; CHECK(fixture.ex.resources.bytes == 0);
        // Repeated temporary results cannot retain cache charges or images.
        for (int i = 0; i < 100; ++i) {
            if (mask) (void)fixture.run(MaskValue{source,{}},sigma);
            else (void)fixture.run(LayerValue{source,{}},sigma);
        }
        CHECK(fixture.ex.resources.bytes == 0);
        const auto op = mask ? Opcode::MaskBlur : Opcode::LayerBlur;
        const Value handle = mask ? Value(MaskValue{source,{}}) : Value(LayerValue{source,{}});
        fixture.ex.fuel = cost-1; fixture.ex.push(handle); fixture.ex.push(sigma);
        try { (void)detail::stepBlur(fixture.ex,{op,0,0}); FAIL("insufficient fuel accepted"); }
        catch (const Trap& error) { CHECK(error.reason == ErrorReason::FuelExhausted); }
        CHECK(fixture.ex.frame().stack.empty()); CHECK(fixture.ex.resources.bytes == 0);
        fixture.ex.fuel = 100000; fixture.ex.resources.bytes = kMaxGenerationData-resultBytes-cacheBytes+1;
        const auto occupied = fixture.ex.resources.bytes;
        fixture.ex.push(handle); fixture.ex.push(sigma);
        try { (void)detail::stepBlur(fixture.ex,{op,0,0}); FAIL("insufficient cache space accepted"); }
        catch (const Trap& error) { CHECK(error.reason == ErrorReason::OutOfMemory); }
        CHECK(fixture.ex.resources.bytes == occupied); CHECK(fixture.ex.frame().stack.empty());
        fixture.ex.resources.bytes = kMaxGenerationData-resultBytes-cacheBytes;
        fixture.ex.push(handle); fixture.ex.push(sigma);
        CHECK_NOTHROW(detail::stepBlur(fixture.ex,{op,0,0}));
        CHECK(fixture.ex.resources.peak == kMaxGenerationData);
        (void)fixture.ex.pop(); CHECK(fixture.ex.resources.bytes == kMaxGenerationData-resultBytes-cacheBytes);
        fixture.ex.resources.bytes = 0;
    }
}

TEST_CASE("blur runtime rejects invalid sigmas handles and encodings before publishing", "[blur]") {
    for (const auto op : {Opcode::LayerBlur,Opcode::MaskBlur}) {
        BlurExecution fixture; const auto source=imageBuffer(1,1);
        const Value handle=op==Opcode::LayerBlur ? Value(LayerValue{source,{}}) : Value(MaskValue{source,{}});
        for (const auto sigma : {-1.f,64.01f,std::numeric_limits<f32>::quiet_NaN(),
                std::numeric_limits<f32>::infinity(),-std::numeric_limits<f32>::infinity()}) {
            fixture.ex.push(handle); fixture.ex.push(sigma);
            try { (void)detail::stepBlur(fixture.ex,{op,0,0}); FAIL("invalid sigma accepted"); }
            catch (const Trap& error) { CHECK(error.reason==ErrorReason::NumericRange); }
            fixture.ex.frame().stack.clear(); CHECK(fixture.ex.resources.bytes==0);
        }
        fixture.ex.push(op==Opcode::LayerBlur ? Value(LayerValue{}) : Value(MaskValue{})); fixture.ex.push(0.f);
        CHECK_THROWS_AS(detail::stepBlur(fixture.ex,{op,0,0}),Trap); CHECK(fixture.ex.frame().stack.empty());
        for (const auto ins : {Instruction{op,1,0},Instruction{op,0,1}}) {
            CHECK_THROWS_AS(detail::stepBlur(fixture.ex,ins),Trap); CHECK(fixture.ex.resources.bytes==0);
        }
        source->raster.pixels.clear(); fixture.ex.push(handle); fixture.ex.push(0.f);
        CHECK_THROWS_AS(detail::stepBlur(fixture.ex,{op,0,0}),Trap); CHECK(fixture.ex.resources.bytes==0);
    }
    for (const bool optimize : {false,true}) for (const auto body : {
        "if(false){layer image{}} image.blur(0)","if(false){layer image{}m=mask.from(image)}m.blur(0)",
        "layer image{}x=image.blur(sigma)","layer image{}x=mask.from(image).blur(sigma)"}) {
        const auto bytes=compileBlur("asset t{size 1x1 input f32 sigma=-1 draw{"+std::string(body)+"}}",optimize);
        const auto file=loadFile(bytes); VM vm(file,{});
        CHECK_THROWS_AS(vm.run(),Trap); CHECK_THROWS_AS(vm.result(),std::logic_error);
        CHECK_THROWS_AS(vm.takeResult(),std::logic_error);
    }
}

TEST_CASE("blur helpers chains aliases argument effects and later clip retain language semantics", "[blur]") {
    const std::string source=R"(asset t{size 7x3 output list point order output point state
      receiver()->layer{order={1,seed&65535} x=0 to 10 layer image(alpha=surface.source_over){pixel(3,1,#ff802080)}return image}
      sigma()->f32{order={2,seed&65535} x=0 to 10 return 1}
      echo(image:layer)->layer{return image.blur(0)}
      soft(m:mask)->mask{return m.blur(sigma=0)}
      blur(x:i32)->i32{return x}
      draw{before=seed image=receiver().blur(sigma()).blur(0.0) alias=image
        layer image{} surface.paint(echo(alias),blend=surface.source_over)
        layer clipImage{rect(3,0,4,3,#fff)}region=soft(mask.from(clipImage))
        clip(region){softened=alias.blur(0) surface.paint(softened,blend=surface.source_over)}
        image.blur(2) x=blur(3) state={seed&65535,seed>>16}}})";
    for (const bool optimize : {false,true}) {
        const auto result=generateBlur(compileBlur(source,optimize),26);
        REQUIRE(result.outputs[0].points.size()==2);
        CHECK(result.outputs[0].points[0].x==1); CHECK(result.outputs[0].points[0].y==26);
        CHECK(result.outputs[0].points[1].x==2);
        const u32 once=26u+0x9e3779b9u, twice=once+0x9e3779b9u;
        CHECK(result.outputs[0].points[1].y==static_cast<f32>(once&65535));
        CHECK(result.outputs[1].points[0].x==static_cast<f32>(twice&65535));
        CHECK(result.outputs[1].points[0].y==static_cast<f32>(twice>>16));
        // A single filtered red source retains its straight color in the halo.
        CHECK(result.image.at(2,1).r==255); CHECK(result.image.at(2,1).a==12);
        CHECK(result.image.at(4,1).a>result.image.at(2,1).a);
        // Filtering inside a clip must include source pixels outside it.
        const std::string outside=R"(asset t{size 7x1 draw{
          layer image(alpha=surface.source_over){pixel(2,0,#ff0000)}
          layer limit{rect(3,0,4,1,#fff)}clip(mask.from(limit)){
            surface.paint(image.blur(1),blend=surface.source_over)}}})";
        const auto clipped=generateBlur(compileBlur(outside,optimize));
        CHECK(clipped.image.at(2,0)==Color{}); CHECK(clipped.image.at(3,0)==Color{255,0,0,62});
    }
}

TEST_CASE("blur compiler formatter tools and format reject malformed instructions", "[blur]") {
    const std::string source="asset t{size 5x3 draw{layer image(alpha=surface.source_over){pixel(2,1,#ff804080)}"
        "copy=image /* receiver */ .blur(/* sigma */ sigma=1).blur(0.5);surface.paint(copy,blend=surface.source_over);"
        "surface.paint(mask.from(image).blur(2),#40a0ff,blend=surface.source_over)}}";
    const auto formatted=ips::formatProgram(ips::parse(source));
    CHECK(formatted.find("/* receiver */")!=std::string::npos); CHECK(formatted.find("/* sigma */")!=std::string::npos);
    CHECK(ips::formatProgram(ips::parse(formatted))==formatted);
    const auto expected=generateBlur(compileBlur(source,false)).image.pixels;
    for (const bool optimize : {false,true}) {
        const auto bytes=compileBlur(source,optimize); const auto file=loadFile(bytes);
        CHECK(file.layout.header.versionMinor==7);
        CHECK(stripDebug(bytes)==stripDebug(compileBlur(formatted,optimize)));
        const auto text=disassemble(bytes); CHECK(text.find("layer.blur")!=std::string::npos); CHECK(text.find("mask.blur")!=std::string::npos);
        for (const auto& candidate : {bytes,stripDebug(bytes),compressFile(bytes),assemble(text)})
            CHECK(generateBlur(candidate).image.pixels==expected);
        CHECK(stripDebug(decompressFile(compressFile(bytes)))==stripDebug(bytes));
        for (const auto& fn : file.code.functions) for (std::size_t i=0;i<fn.code.size();i+=4) {
            const auto ins=decodeInstruction(fn.code.subspan(i,4));
            if (ins.opcode!=Opcode::LayerBlur && ins.opcode!=Opcode::MaskBlur) continue;
            for (const auto relative : {1u,2u,3u}) {
                auto bad=bytes; bad.at(fn.fileOffset+i+relative)=1;
                CHECK_THROWS_AS(validate(loadFile(bad)),FormatError);
            }
        }
    }
    for (const auto mnemonic : {"layer.blur","mask.blur"}) for (const auto operand : {"","const.i32 1\n","const.f32 1.0\n"})
        CHECK_THROWS_AS(assemble("meta: size=1x1\nfn draw ( ) -> ( ):\n"+std::string(operand)+mnemonic+"\ndrop\nendfn\n"),std::exception);
    // Operand/aux validation also covers unreachable instructions.
    const auto dead=assemble("meta: size=1x1\nfn draw ( ) -> ( ):\nreturn\nlayer.blur\nmask.blur\nendfn\n");
    const auto loaded=loadFile(dead);
    for (const auto offset : {4u,8u}) for (const auto relative : {1u,2u}) {
        auto bad=dead; bad.at(loaded.code.functions[0].fileOffset+offset+relative)=1;
        CHECK_THROWS_AS(validate(loadFile(bad)),FormatError);
    }
    for (u16 minor=0;minor<7;++minor) { auto old=dead; old.at(6)=static_cast<u8>(minor); CHECK_THROWS_AS(loadFile(old),FormatError); }
    CHECK(ips::surfaceMethodSpecs().at("blur").overloads.size()==2);
    for (const auto body : {"layer image{}image.blur()","layer image{}image.blur(1,2)",
        "layer image{}image.blur(radius=1)","layer image{}image.blur(sigma=1,sigma=2)"})
        reject("asset t{size 1x1 draw{"+std::string(body)+"}}",".blur takes one f32 sigma");
    for (const auto body : {"x=(1).blur(1)","x=[1].blur(1)","x=(#fff).blur(1)","x=sampler(1).blur(1)"})
        reject("asset t{size 1x1 draw{"+std::string(body)+"}}","layer or mask receiver");
    for (const auto sigma : {"#fff","{1,2}","[1]"})
        reject("asset t{size 1x1 draw{layer image{}image.blur("+std::string(sigma)+")}}","sigma must be f32");
    for (const auto sigma : {"-1","64.1","65","2*40"})
        reject("asset t{size 1x1 draw{layer image{}image.blur("+std::string(sigma)+")}}","finite and in 0..64");
    reject("asset t{size 1x1 const f32 bad=65 draw{layer image{}mask.from(image).blur(bad)}}","finite and in 0..64");
    reject("asset t{size 1x1 draw{x=blur(1)}}","undefined function 'blur'");
}
