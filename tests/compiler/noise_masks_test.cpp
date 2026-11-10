#include <catch2/catch_test_macros.hpp>
#include "Compiler.hpp"
#include "SourceCompilation.hpp"
#include "iprop/Noise.hpp"
#include "iprop/VmInternals.hpp"
#include "iprop/VM.hpp"
#include "ips/Parser.hpp"
#include "ips/Printer.hpp"
#include "ips/Semantics.hpp"
#include "support/test_support.hpp"
#include "support/compiler_support.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

using namespace iprop;
using compiler_test::compileChecked;
using namespace test_support;
namespace {
struct Args {
    Vec2 origin{0,0}, size{4,3};
    i32 kind = 0;
    Vec2 frequency{0.31f,0.27f};
    f32 threshold = 0.3f;
    i32 seed = 7, octaves = 4;
    f32 softness = 0;
    i32 comparison = 0;
    f32 warp = 0;
};
void invoke(detail::Execution& ex, const Args& a, bool full = true) {
    ex.push(a.origin); ex.push(a.size); ex.push(a.kind); ex.push(a.frequency); ex.push(a.threshold); ex.push(a.seed);
    if (full) { ex.push(a.octaves); ex.push(a.softness); ex.push(a.comparison); ex.push(a.warp); }
    if (!detail::stepMasks(ex, {full ? Opcode::NoiseMaskConfig : Opcode::NoiseMask,0,0}))
        throw std::logic_error("noise mask opcode was not dispatched");
}
MaskValue generate(detail::Execution& ex, const Args& a, bool full = true) {
    invoke(ex,a,full); return std::get<MaskValue>(ex.pop());
}
f32 scalar(Vec2 p, const Args& a) {
    if (a.warp != 0) p = std::get<Vec2>(evalProg(Prog{}.F(p.x).F(p.y).F(a.warp).I(a.seed).emit(0xe1))[0]);
    const auto sample = [&](u8 opcode) {
        auto program = Prog{}.F(p.x).F(p.y);
        if (opcode == 0x8a || opcode == 0x8e) program.I(a.octaves);
        program.I(a.seed).emit(opcode); return evalProgF32(program);
    };
    if (a.kind == 4) return sample(0xdf)-sample(0x8b);
    constexpr u8 opcodes[]{0x70,0x89,0x8a,0x8b,0xdf,0x8e};
    return sample(opcodes[a.kind]);
}
u8 quantize(f32 value, const Args& a) {
    f32 alpha;
    if (a.softness == 0) alpha = (a.comparison == 0 ? value >= a.threshold : value <= a.threshold) ? 1.f : 0.f;
    else {
        const auto t=std::clamp((value-a.threshold)/a.softness+0.5f,0.f,1.f);
        alpha=t*t*(3.f-2.f*t); if (a.comparison) alpha=1.f-alpha;
    }
    return static_cast<u8>(std::floor(alpha*255.f+0.5f));
}
}

TEST_CASE("noise masks match scalar pixel-center sampling in every mode and scale", "[noise-mask-v09]") {
    const auto file=loadFile(compileChecked("asset t{size 4x3 draw{}}"));
    detail::Execution ex(file); ex.frames.emplace_back(); ex.fuel=100000000;
    for (const auto scale : {Vec2{1,1},Vec2{2,0.5f},Vec2{0.75f,1.5f}}) {
        ex.target=RenderTarget::create(8,6,scale);
        for (i32 kind=0;kind<6;++kind) for (const auto origin:{Vec2{0,0},Vec2{-1.25f,0.25f}})
            for (const f32 softness:{0.f,0.2f}) for (const i32 comparison:{0,1}) for (const f32 warp:{0.f,0.3f}) {
                Args a; a.kind=kind; a.origin=origin; a.softness=softness; a.comparison=comparison; a.warp=warp;
                a.octaves=kind%2 ? 1 : 8;
                INFO(kind << ' ' << scale.x << ' ' << scale.y << ' ' << softness << ' ' << comparison << ' ' << warp);
                auto mask=generate(ex,a); const auto bounds=maskBounds(mask);
                CHECK(mask.buffer->format==SurfaceBuffer::Format::A8); CHECK(mask.buffer->raster.pixels.empty());
                CHECK(mask.buffer->raster.scaleX==scale.x); CHECK(mask.buffer->raster.scaleY==scale.y);
                for(u32 y=0;y<bounds.height;++y)for(u32 x=0;x<bounds.width;++x){
                    const auto px=bounds.x+static_cast<i32>(x),py=bounds.y+static_cast<i32>(y);
                    const Vec2 local{(static_cast<f32>(px)+0.5f)/scale.x,(static_cast<f32>(py)+0.5f)/scale.y};
                    const bool inside=local.x>=origin.x&&local.y>=origin.y&&local.x<origin.x+a.size.x&&local.y<origin.y+a.size.y;
                    const u8 expected=inside?quantize(scalar({local.x*a.frequency.x,local.y*a.frequency.y},a),a):0;
                    CHECK(maskCoverage(mask,px,py)==expected);
                }
                CHECK(maskCoverage(mask,bounds.x-1,bounds.y)==0);
            }
        CHECK(ex.resources.bytes==0);
    }
}

TEST_CASE("noise mask validation and sample traps never publish or retain provisional buffers", "[noise-mask-v09]") {
    const auto file=loadFile(compileChecked("asset t{size 1x1 draw{}}"));
    detail::Execution ex(file); ex.frames.emplace_back(); ex.target=RenderTarget::create(1,1,{1,1}); ex.fuel=100000000;
    const f32 inf=std::numeric_limits<f32>::infinity(),nan=std::numeric_limits<f32>::quiet_NaN();
    const auto reject=[&](const Args& a,const char* message){
        try {invoke(ex,a);FAIL("invalid noise mask accepted");} catch(const Trap& error){
            CHECK(error.reason==ErrorReason::NumericRange); CHECK(std::string(error.what()).find(message)!=std::string::npos);
        }
        CHECK(ex.frame().stack.empty()); CHECK(ex.resources.bytes==0);
    };
    for(const f32 bad:{inf,-inf,nan}) for(int component=0;component<2;++component){
        Args a; (component?a.origin.y:a.origin.x)=bad; reject(a,"rectangle");
        a={}; (component?a.size.y:a.size.x)=bad; reject(a,"rectangle");
        a={}; (component?a.frequency.y:a.frequency.x)=bad; reject(a,"frequency");
    }
    for(const f32 bad:{0.f,-0.f,-1.f})for(int component=0;component<2;++component){
        Args a;(component?a.size.y:a.size.x)=bad;reject(a,"rectangle");
        a={};(component?a.frequency.y:a.frequency.x)=bad;reject(a,"frequency");
    }
    for(const i32 bad:{-1,6,std::numeric_limits<i32>::max()}){Args a;a.kind=bad;reject(a,"kind");}
    for(i32 mode=0;mode<6;++mode)for(const i32 n:{0,9,-1,std::numeric_limits<i32>::max()}){Args a;a.kind=mode;a.octaves=n;reject(a,"octaves");}
    for(const f32 bad:{inf,-inf,nan}){
        Args a;a.threshold=bad;reject(a,"threshold");a={};a.softness=bad;reject(a,"softness");
        a={};a.warp=bad;reject(a,"warp amount");
    }
    {Args a;a.softness=-1;reject(a,"softness");a={};a.comparison=2;reject(a,"comparison");
     a={};a.origin={2147483648.f,0};a.size={256,1};reject(a,"mapping");
     a={};a.origin={16777216.f,0};a.size={0.25f,1};reject(a,"rectangle");}
    for(i32 mode=0;mode<6;++mode){
        Args a;a.kind=mode;a.frequency={std::numeric_limits<f32>::max(),1};reject(a,"noise coordinate");
        a.frequency={1,1};a.warp=std::numeric_limits<f32>::max();reject(a,"noise coordinate");
    }
    for(const f32 bad:{0.f,-1.f,inf,nan}){
        ex.canvas().scaleX=bad;reject(Args{},"raster scale");ex.canvas().scaleX=1;
    }
}

TEST_CASE("noise mask fuel memory zero cases and lifetime are bounded before work", "[noise-mask-v09]") {
    const auto file=loadFile(compileChecked("asset t{size 1x1 draw{}}"));
    detail::Execution ex(file);ex.frames.emplace_back();ex.target=RenderTarget::create(1,1,{1,1});
    for(i32 mode=0;mode<6;++mode)for(i32 n:{1,4,8})for(f32 warp:{0.f,-0.f,0.5f}){
        Args a;a.kind=mode;a.octaves=n;a.warp=warp;a.origin={20,30};
        const u64 expected=12u*(2u+(mode==3||mode==4?25u:mode==2||mode==5?4u*static_cast<u64>(n):4u)+(warp==0?0u:32u));
        ex.fuel=expected;auto mask=generate(ex,a);CHECK(ex.fuel==0);CHECK(ex.resources.bytes==140);
        auto alias=mask;mask={};CHECK(ex.resources.bytes==140);alias={};CHECK(ex.resources.bytes==0);
        ex.fuel=expected-1;CHECK_THROWS_AS(invoke(ex,a),Trap);CHECK(ex.frame().stack.empty());CHECK(ex.resources.bytes==0);
    }
    ex.fuel=10000;ex.resources.bytes=kMaxGenerationData-139;
    CHECK_THROWS_AS(invoke(ex,Args{}),Trap);CHECK(ex.resources.bytes==kMaxGenerationData-139);CHECK(ex.frame().stack.empty());ex.resources.bytes=0;
    ex.fuel=10000;Args tiny;tiny.size={0.1f,0.1f};tiny.threshold=-100;
    auto small=generate(ex,tiny);CHECK(small.buffer->coverage==std::vector<u8>{0});small={};
    Args equality;equality.size={1,1};equality.kind=1;equality.frequency={2,2};equality.threshold=0;
    for(const i32 comparison:{0,1}){equality.comparison=comparison;CHECK(generate(ex,equality).buffer->coverage==std::vector<u8>{255});}
    equality.softness=0.2f;CHECK(generate(ex,equality).buffer->coverage==std::vector<u8>{128});
    Args defaults;auto shortMask=generate(ex,defaults,false),longMask=generate(ex,defaults);
    CHECK(shortMask.buffer->coverage==longMask.buffer->coverage);
}

TEST_CASE("noise masks compose with clips algebra blur morphology and preserve generation RNG", "[noise-mask-v09]") {
    const std::string setup="m=noise.mask({-1,0},{5,3},noise.mode_cell_edge,{0.4,0.3},0.2,7,4,0.1,noise.below,0.3)";
    const std::string source="asset t{size 4x3 output point state output mask result identity(m:mask)->mask{return m}draw{before=seed "+setup+
        " m=identity(m)result=m surface.paint(mask.expand(m,0),#fff,blend=surface.source_over)"
        "clip(mask.union(m,m)){surface.paint(mask.invert(mask.subtract(m,m)).blur(0),#12345680)}"
        "state={seed==before,unit()}}}";
    const auto parsed=ips::parse(source);const auto formatted=ips::formatProgram(parsed);
    for(const bool opt:{false,true}){
        const auto bytes=compileChecked(source,opt);
        const auto file=loadFile(bytes);validate(file);VM vm(file,{26,8,6,{}});vm.run();auto result=vm.takeResult();
        CHECK(result.image.width==8); CHECK(result.image.height==6);
        const auto roundtripFile=loadFile(compileChecked(formatted,opt));validate(roundtripFile);
        VM roundtrip(roundtripFile,{26,8,6,{}});roundtrip.run();
        CHECK(roundtrip.result().image.pixels==result.image.pixels);
        const auto controlFile=loadFile(compileChecked("asset t{size 4x3 output point state draw{state={1,unit()}}}"));VM control(controlFile,{26,8,6,{}});control.run();
        CHECK(result.outputs[0].points[0].x==1);CHECK(result.outputs[0].points[0].y==control.result().outputs[0].points[0].y);
    }
    for(const std::string body:{"output mask result draw{result=noise.mask({0,0},{2,1},0,{1.0/0.0,1},0,7)}",
        "output mask result draw{result=noise.mask({0,0},{2,1},0,{1,1},0,7)bad=noise.mask({0,0},{2,1},0,{1.0/0.0,1},0,7)}"}){
        const auto file=loadFile(compileChecked("asset t{size 2x1 "+body+"}"));validate(file);VM vm(file,{});CHECK_THROWS_AS(vm.run(),Trap);CHECK_THROWS_AS(vm.takeResult(),std::logic_error);
    }
}

TEST_CASE("noise mask arguments bind once in signature order and constants remain scalar", "[noise-mask-v09]") {
    const std::string prefix="asset t{size 1x1 const i32 mode=noise.mode_cell_edge const i32 cmp=noise.below output list point trace "
        "v(n:i32)->vec2{trace={n,0}return {1,1}}i(n:i32)->i32{trace={n,0}return 1}f(n:i32)->f32{trace={n,0}return 0.2}draw{";
    const std::string call="m=noise.mask(warp_amount=f(10),comparison=i(9),softness=f(8),octaves=i(7),seed=i(6),threshold=f(5),frequency=v(4),mode=i(3),size=v(2),origin=v(1))";
    for(bool opt:{false,true}){
        const auto file=loadFile(compileChecked(prefix+call+"}}",opt));validate(file);VM vm(file,{});vm.run();const auto result=vm.result();const auto& trace=result.outputs[0].points;
        REQUIRE(trace.size()==10);for(std::size_t j=0;j<10;++j)CHECK(trace[j].x==static_cast<f32>(j+1));
    }
    for(const std::string bad:{"const mask m=noise.mask({0,0},{1,1},0,{1,1},0,7)","draw{m=noise.mask({0,0},{1,1},0,{1,1},0,7,4)}",
         "draw{m=noise.mask({0,0},{1,1},0.5,{1,1},0,7)}","draw{m=noise.mask({0,0},{1,1},noise.unknown,{1,1},0,7)}"}){
        const auto result=compileSource("bad.ips",[&](std::string_view){return "asset t{size 1x1 "+bad+"}";});CHECK_FALSE(result.succeeded());CHECK(result.bytes.empty());
    }
    CHECK_FALSE(detail::isConstantOperation(*findOpcode(Opcode::NoiseMask)));
    CHECK_FALSE(detail::isConstantOperation(*findOpcode(Opcode::NoiseMaskConfig)));
}

TEST_CASE("noise mask views retain D4 through positive morphology blur and export sharing", "[noise-mask-v09]") {
    const auto file=loadFile(compileChecked("asset t{size 8x8 output mask result draw{}}"));
    detail::Execution ex(file);ex.frames.emplace_back();ex.target=RenderTarget::create(8,8,{1,1});ex.fuel=10000000;
    Args a;a.kind=4;a.softness=0.2f;a.warp=0.3f;
    auto generated=generate(ex,a);
    auto buffer=std::make_shared<SurfaceBuffer>();buffer->format=SurfaceBuffer::Format::A8;
    buffer->raster.width=4;buffer->raster.height=3;
    for(u32 y=0;y<3;++y)for(u32 x=0;x<4;++x)
        buffer->coverage.push_back(quantize(scalar({(static_cast<f32>(x)+0.5f)*a.frequency.x,(static_cast<f32>(y)+0.5f)*a.frequency.y},a),a));
    MaskValue manual{buffer,{}};
    for(u8 kind=0;kind<8;++kind){
        generated.mapping={*D4Transform::fromKind(kind),{-2,3}};manual.mapping=generated.mapping;
        for(const auto op:{Opcode::MaskExpand,Opcode::MaskShrink,Opcode::MaskBlur}){
            const auto apply=[&](const MaskValue& mask){
                ex.push(mask);
                if(op==Opcode::MaskBlur){ex.push(0.7f);if(!detail::stepBlur(ex,{op,0,0}))throw std::logic_error("blur dispatch");}
                else{ex.push(i32{1});if(!detail::stepMasks(ex,{op,0,0}))throw std::logic_error("morph dispatch");}
                return std::get<MaskValue>(ex.pop());
            };
            const auto actual=apply(generated),expected=apply(manual);
            CHECK(actual.buffer->coverage==expected.buffer->coverage);
            CHECK(actual.mapping.origin==expected.mapping.origin);
            CHECK(maskBounds(actual).width==maskBounds(expected).width);
            CHECK(maskBounds(actual).height==maskBounds(expected).height);
        }
    }
    ex.resourceSlots={MaskValue{}};ex.push(generated);
    REQUIRE(detail::stepResourceExports(ex,{Opcode::OutMask,0,0}));
    const auto exported=std::get<MaskValue>(ex.resourceSlots[0]);CHECK(exported.buffer==generated.buffer);
    CHECK(exported.mapping.origin==generated.mapping.origin);
    CHECK(ex.resources.bytes==140);
}
