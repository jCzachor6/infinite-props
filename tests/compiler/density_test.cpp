#include <catch2/catch_test_macros.hpp>
#include "Compiler.hpp"
#include "iprop/VM.hpp"
#include "iprop/VmInternals.hpp"
#include "iprop/Compression.hpp"
#include "iprop/Debug.hpp"
#include "iprop/math/Distribution.hpp"
#include "ips/Parser.hpp"
#include "ips/Printer.hpp"
#include "ips/Semantics.hpp"
#include "ipa.hpp"
#include "ipdis.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>

using namespace iprop;
namespace {
Distribution density(std::initializer_list<DistributionPoint> points) {
    auto data=std::make_shared<PointDistributionData>();data->family=DistributionFamily::Density;data->points=points;
    REQUIRE(math::pointDistributionError(*data)==nullptr);
    Distribution d;d.family=data->family;d.points=std::move(data);return d;
}
std::vector<u8> compileDensity(const std::string& source,bool optimize=true) {
    auto p=ips::parse(source);ips::checkAsset(p,{});return compileProgram(p,{},optimize);
}
GenerationResult generateDensity(const std::vector<u8>& bytes,u32 seed=26,u32 variant=0) {
    const auto file=loadFile(bytes);validate(file);VM vm(file,{seed,variant,0,0,0,{}});vm.run();return vm.result();
}
std::vector<u32> densityBits(const GenerationResult& r) {
    std::vector<u32> bits;for(const auto& out:r.outputs)for(const auto& p:out.points){bits.push_back(std::bit_cast<u32>(p.x));bits.push_back(std::bit_cast<u32>(p.y));}return bits;
}
std::string densityAsset(const std::string& declarations,const std::string& body,const std::string& helpers={}) {
    return "asset t{size 16x16 variants 2 output list point p output point state "+declarations+helpers+
        " draw{"+body+" state={seed&65535,seed>>16}}}";
}
void rejectDensity(const std::string& source,const std::string& message) {
    try{(void)compileDensity(source);FAIL("invalid density accepted");}
    catch(const ips::SourceError& error){INFO(error.what());CHECK(std::string(error.what()).find(message)!=std::string::npos);CHECK(error.location.line>0);CHECK(error.location.column>0);}
}
u32 densityHash(u32 x){x^=x>>16;x*=0x7feb352d;x^=x>>15;x*=0x846ca68b;return x^(x>>16);}
double ks(std::vector<double> values,const auto& cdf){
    std::sort(values.begin(),values.end());double worst=0;const auto n=static_cast<double>(values.size());
    for(std::size_t i=0;i<values.size();++i){const double f=cdf(values[i]);worst=std::max({worst,std::fabs(f-i/n),std::fabs(f-(i+1)/n)});}return worst;
}
}

TEST_CASE("density analytic CDFs clip and invert uniform triangles plateau and zero segments", "[density]") {
    const auto flat=density({{i32{0},i32{1}},{i32{1},i32{1}}});
    const auto rising=density({{i32{0},i32{0}},{i32{1},i32{2}}});
    const auto falling=density({{i32{0},i32{2}},{i32{1},i32{0}}});
    const auto plateau=density({{i32{0},i32{0}},{i32{1},i32{1}},{i32{3},i32{1}},{i32{4},i32{0}}});
    const auto zeros=density({{i32{0},i32{0}},{i32{1},i32{0}},{i32{2},i32{1}},{i32{3},i32{0}},{i32{4},i32{0}}});
    const auto clippedRise=density({{i32{0},i32{0}},{i32{4},i32{2}}});
    const auto clippedFall=density({{i32{0},i32{2}},{i32{4},i32{0}}});
    for(const double u:{0x1p-33,0.125,0.25,0.5,0.75,1.0-0x1p-33}) {
        for(const auto& [shape,expected]:std::initializer_list<std::pair<Distribution,double>>{
            {flat,u},{rising,std::sqrt(u)},{falling,u/(1.0+std::sqrt(1.0-u))}}) {
            const auto draw=math::truncatedDraw(shape,0,1,u);CHECK(draw.status==math::DrawStatus::Ok);
            CHECK(std::bit_cast<u32>(static_cast<f32>(draw.value))==std::bit_cast<u32>(static_cast<f32>(expected)));
        }
        const double z=u*3;const double p=z<0.5?std::sqrt(2*z):z<2.5?1+(z-0.5):4-std::sqrt(2*(3-z));
        CHECK(std::fabs(math::truncatedDraw(plateau,0,4,u).value-p)<2e-15);
        CHECK(math::truncatedDraw(zeros,0,4,u).value>=1);CHECK(math::truncatedDraw(zeros,0,4,u).value<=3);
        CHECK(std::fabs(math::truncatedDraw(clippedRise,1,3,u).value-std::sqrt(1+8*u))<1e-14);
        CHECK(std::fabs(math::truncatedDraw(clippedFall,1,3,u).value-(4-std::sqrt(9-8*u)))<1e-14);
    }
    CHECK(math::truncatedDraw(plateau,0,4,1.0/6.0).value==1);
    CHECK(math::truncatedDraw(plateau,0,4,5.0/6.0).value==3);
    CHECK(math::truncatedDraw(zeros,0,1,0.5).status==math::DrawStatus::NoMass);
    CHECK(math::truncatedDraw(zeros,1,1,0.5).value==1); // zero height is still inside closed support
    CHECK(math::truncatedDraw(zeros,4,4,0.5).value==4);
    CHECK(math::truncatedDraw(zeros,5,5,0.5).status==math::DrawStatus::Outside);
    CHECK(math::truncatedDraw(zeros,4,5,0.5).status==math::DrawStatus::NoMass);
    CHECK(math::pointDrawWork(zeros,0,4)==8);CHECK(math::pointDrawWork(zeros,1,1)==0);
}

TEST_CASE("density inversion pins radicand correction and tiny and extreme coordinate scales", "[density]") {
    const auto edge=math::invertDensitySegment(1,1,0,std::nextafter(0.5,1.0));
    CHECK(edge.status==math::DrawStatus::Ok);CHECK(edge.value==1);
    CHECK(math::invertDensitySegment(1,1,0,0.5*(1+1e-12)).status==math::DrawStatus::Numerical);
    CHECK(math::invertDensitySegment(1,0,1,0).value==0);
    CHECK(math::invertDensitySegment(1,1,1,0.25).value==0.25);
    CHECK(math::invertDensitySegment(1,0,0,1).status==math::DrawStatus::Numerical);
    const f32 tiny=std::numeric_limits<f32>::denorm_min(),big=std::numeric_limits<f32>::max();
    for(const auto& shape:{density({{0.f,tiny},{tiny,tiny}}),density({{0.f,0.f},{tiny,big}}),
        density({{-big,big},{big,big}}),density({{i32{16777217},1.f},{16777218.f,1.f},{i32{16777219},1.f}})}) {
        const double lo=math::distributionNumber(shape.points->points.front().key),hi=math::distributionNumber(shape.points->points.back().key);
        for(const double u:{0x1p-33,0.25,0.5,1-0x1p-33}){
            const auto draw=math::truncatedDraw(shape,lo,hi,u);CHECK(draw.status==math::DrawStatus::Ok);
            CHECK(std::isfinite(draw.value));CHECK(draw.value>=lo);CHECK(draw.value<=hi);
        }
    }
    const auto tinyArea=density({{0.f,tiny},{tiny,tiny}});
    CHECK(math::truncatedDraw(tinyArea,0,tiny,0.5).value==static_cast<double>(tiny)*0.5);
    const auto large=density({{i32{16777217},i32{1}},{i32{16777219},i32{1}}});
    CHECK(math::truncatedDraw(large,16777217,16777219,0.5).value==16777218);
    const auto nearlyFlat=density({{0.f,1e-30f},{1.f,std::nextafter(1e-30f,2e-30f)}});
    CHECK(std::fabs(math::truncatedDraw(nearlyFlat,0,1,0.5).value-0.5)<1e-7);
}

TEST_CASE("density samples follow independent analytical CDFs", "[density]") {
    const auto sample=[](const std::string& expression){
        const auto r=generateDensity(compileDensity(densityAsset("","for i in 0..5000{p={"+expression+",0}}")),73);
        std::vector<double> values;for(const auto& p:r.outputs[0].points)values.push_back(p.x);return values;
    };
    CHECK(ks(sample("0.0 to 1.0 with density{0:1,1:1}"),[](double x){return x;})<0.03);
    CHECK(ks(sample("0.0 to 1.0 with density{0:0,1:2}"),[](double x){return x*x;})<0.03);
    CHECK(ks(sample("0.0 to 1.0 with density{0:2,1:0}"),[](double x){return 2*x-x*x;})<0.03);
    CHECK(ks(sample("1.0 to 3.0 with density{0:0,4:2}"),[](double x){return (x*x-1)/8;})<0.03);
    CHECK(ks(sample("0.0 to 4.0 with density{0:0,1:1,3:1,4:0}"),[](double x){return x<1?x*x/6:x<3?(x-0.5)/3:1-(4-x)*(4-x)/6;})<0.03);
}

TEST_CASE("density native draws pin the full raw stream and integer rounding", "[density]") {
    for(const bool optimize:{false,true})for(const auto& [literal,expected]:std::initializer_list<std::pair<const char*,int>>{
        {"density{0:1,1:1}",0},{"density{0:0,1:2}",1},{"density{0:2,1:0}",2}}) {
        const auto bytes=compileDensity(densityAsset("","d="+std::string(literal)+" for i in 0..64{p={0.0 to 1.0 with d,0}}"),optimize);
        for(const u32 seed:{0u,1u,7u,26u,0x61c88647u,0xffffffffu}){
            u32 stream=seed;const auto result=generateDensity(bytes,seed);
            for(const auto& p:result.outputs[0].points){stream+=0x9e3779b9u;const double u=(static_cast<double>(densityHash(stream))+0.5)*0x1p-32;
                const double value=expected==0?u:expected==1?std::sqrt(u):u/(1+std::sqrt(1-u));
                CHECK(std::bit_cast<u32>(p.x)==std::bit_cast<u32>(static_cast<f32>(value)));}
            CHECK(result.outputs[1].points[0].x==static_cast<f32>(stream&65535));CHECK(result.outputs[1].points[0].y==static_cast<f32>(stream>>16));
        }
        const auto rounded=generateDensity(compileDensity(densityAsset("","p={(-2) to 2 with density{(-2):1,2:1},0}"),optimize),0x61c88647);
        CHECK(rounded.outputs[0].points[0].x==-2);
    }
}

TEST_CASE("density snapshots and mixed distribution pools share captures and formatter transport", "[density]") {
    const std::string declarations="const distribution shape=density{lo:0,1:2,2:0} const i32 lo=0 "
        "const list<distribution> shapes=[weighted{0:1,1:1,2:1},shape,beta(2,3,0,2)] "
        "const sampler<f32> s=sampler(0.0 to 2.0 with shape) const list<sampler<f32>> specs=[s,s] palette widths=[0.0 to 2.0 with shape,0.5]";
    const auto helpers="echo(value:distribution)->distribution{return value}";
    const auto source=densityAsset(declarations,"before=seed ds=shapes copy=echo(ds[1]) items=specs p={seed==before,0} "
        "values=items[0].pick(3) for i in 0..3{p={values[i],0}} p={widths[0].pick(),0.0 to 2.0 with copy} "
        "for i in 0..3{p={0.0 to 2.0 with ds[i],0}}",helpers);
    const auto reference=compileDensity(densityAsset("","d=density{0:0,1:2,2:0} p={1,0} for i in 0..3{p={0.0 to 2.0 with d,0}} "
        "p={0.0 to 2.0 with d,0.0 to 2.0 with d} p={0.0 to 2.0 with weighted{0:1,1:1,2:1},0} "
        "p={0.0 to 2.0 with d,0} p={0.0 to 2.0 with beta(2,3,0,2),0}"),false);
    for(const bool optimize:{false,true}){
        const auto bytes=compileDensity(source,optimize),restored=assemble(disassemble(bytes));const auto file=loadFile(bytes);
        REQUIRE(file.collections.distributions.size()==2);REQUIRE(!file.collections.samplers.empty());
        const auto shape=std::find_if(file.collections.distributions.begin(),file.collections.distributions.end(),[](const auto& d){return d.family==DistributionFamily::Density;});
        REQUIRE(shape!=file.collections.distributions.end());
        CHECK(file.collections.samplers[0].data->distribution.points==shape->points);
        CHECK(stripDebug(bytes)==stripDebug(compileDensity(ips::formatProgram(ips::parse(source)),optimize)));
        for(const u32 seed:{0u,7u,26u,0xffffffffu})for(const auto& binary:{bytes,restored,compressFile(bytes)})
            CHECK(densityBits(generateDensity(binary,seed))==densityBits(generateDensity(reference,seed)));
    }
    const auto dynamic=densityAsset("","key=1 height=2 before=seed d=density{0:0,key:height,2:0} p={seed==before,0} "
        "key=99 height=0 copy=d d=density{0:1,2:1} p={0.0 to 2.0 with copy,0}");
    const auto dynamicRef=compileDensity(densityAsset("","p={1,0} p={0.0 to 2.0 with density{0:0,1:2,2:0},0}"),false);
    for(const bool optimize:{false,true}) CHECK(densityBits(generateDensity(compileDensity(dynamic,optimize)))==densityBits(generateDensity(dynamicRef)));
}

TEST_CASE("density rejects invalid static data and traps unused runtime snapshots", "[density]") {
    for(const auto& [literal,message]:std::initializer_list<std::pair<const char*,const char*>>{
        {"density{}","between 2 and 256"},{"density{0:1}","between 2 and 256"},{"density{0:0,1:0}","positive total area"},
        {"density{0:(-1),1:1}","nonnegative"},{"density{1:1,0:1}","strictly increasing"},{"density{0:1,0:1}","strictly increasing"},
        {"density{0:1,1:(1.0/0.0)}","finite"},{"density{0:1,#fff:1}","numeric"}}) {
        rejectDensity(densityAsset("const distribution bad="+std::string(literal),""),message);
        rejectDensity(densityAsset("","bad="+std::string(literal)),message);
    }
    rejectDensity(densityAsset("const distribution bad=density{0:1,seed:1}",""),"only asset constants");
    rejectDensity(densityAsset("const distribution bad=density{0:unit(),1:1}",""),"RNG");
    for(const bool optimize:{false,true}) {
        for(const auto& body:{"d=density{0:(v-1),1:1}","d=density{v:1,0:1}","d=density{0:v,1:v}","d=density{0:float(v)/float(v),1:1}"})
            CHECK_THROWS_AS(generateDensity(compileDensity(densityAsset("",body),optimize),0,0),Trap);
        CHECK_THROWS_AS(generateDensity(compileDensity(densityAsset("","d=density{0:0,1:0,2:1} p={0.0 to 1.0 with d,0}"),optimize)),Trap);
        CHECK_THROWS_AS(generateDensity(compileDensity(densityAsset("","d=density{0:0,1:1} p={2.0 to 2.0 with d,0}"),optimize)),Trap);
        CHECK_NOTHROW(generateDensity(compileDensity(densityAsset("","s=sampler(2.0 to 3.0 with density{0:0,1:1}) values=s.pick(0) p={list.len(values),0}"),optimize)));
    }
}

TEST_CASE("density contextual syntax maximum points closed support and fuel remain bounded", "[density]") {
    const auto source=densityAsset("","density=3 x=density(4) d=density{0:0,/* ramp */1:1,2:0,} p={density,x}","density(x:i32)->i32{return x+1}");
    const auto formatted=ips::formatProgram(ips::parse(source));CHECK(ips::formatProgram(ips::parse(formatted))==formatted);
    CHECK(formatted.find("/* ramp */")!=std::string::npos);CHECK(stripDebug(compileDensity(source))==stripDebug(compileDensity(formatted)));
    std::string literal="density{";for(int i=0;i<256;++i){if(i)literal+=",";literal+=std::to_string(i)+":1";}literal+="}";
    for(const bool optimize:{false,true}) {
        CHECK_NOTHROW(generateDensity(compileDensity(densityAsset("const distribution d="+literal,"runtime="+literal+" p={0.0 to 255.0 with runtime,0}"),optimize)));
        rejectDensity(densityAsset("","d="+literal.substr(0,literal.size()-1)+",256:1}"),"between 2 and 256");
    }
    const auto file=loadFile(compileDensity(densityAsset("","p={0.0 to 1.0 with density{0:0,1:1},0}"),false));
    for(const double bound:{0.0,0.5,1.0,2.0}){
        detail::Execution ex(file);ex.frames.push_back({});ex.fuel=1;ex.frame().stream=26;
        ex.push(static_cast<f32>(bound));ex.push(static_cast<f32>(bound));ex.push(density({{0.f,0.f},{1.f,1.f}}));
        if(bound<=1){CHECK(detail::stepSeeded(ex,{Opcode::RngFloatWith,0,0}));CHECK(std::get<f32>(ex.pop())==static_cast<f32>(bound));}
        else CHECK_THROWS_AS(detail::stepSeeded(ex,{Opcode::RngFloatWith,0,0}),Trap);
        CHECK(ex.frame().stream==26u+0x9e3779b9u);CHECK(ex.fuel==1);
    }
}

TEST_CASE("point family names remain numeric upper bounds before loop bodies", "[density]") {
    const auto source=densityAsset("input i32 density=2 input i32 weighted=3",
        "for i in 0..density{p={i,0} d=density{0:1,1:1}} for i in 0..weighted{p={i,1}} "
        "for i in 0..count(density{0:0,1:1}){p={i,2}}",
        "count(d:distribution)->i32{return 2}");
    const auto formatted=ips::formatProgram(ips::parse(source));
    CHECK(ips::formatProgram(ips::parse(formatted))==formatted);
    for(const bool optimize:{false,true}) {
        const auto actual=generateDensity(compileDensity(source,optimize));
        REQUIRE(actual.outputs[0].points.size()==7);
        CHECK(stripDebug(compileDensity(source,optimize))==stripDebug(compileDensity(formatted,optimize)));
    }
}
