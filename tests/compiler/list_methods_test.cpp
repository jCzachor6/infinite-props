#include <catch2/catch_test_macros.hpp>
#include "Compiler.hpp"
#include "SourceCompilation.hpp"
#include "iprop/ListOperations.hpp"
#include "iprop/VmInternals.hpp"
#include "iprop/VM.hpp"
#include "ips/Parser.hpp"
#include "ips/Printer.hpp"
#include "ips/Semantics.hpp"
#include "ipa.hpp"
#include "ipdis.hpp"

#include <algorithm>
#include <bit>
#include <limits>

using namespace iprop;
namespace {
ListValue list(TypeDescriptor item, std::vector<Value> values) {
    auto data=std::make_shared<ListData>(ListData{ValueType::list(item),std::move(values)});
    return {data->type,std::move(data)};
}
ListValue transformed(const ListValue& input, bool sort) {return transformList(input,sort);}
std::vector<u8> compile(const std::string& source, bool optimize=false) {
    auto program=ips::parse(source);ips::checkAsset(program,{});return compileProgram(program,{},optimize);
}
GenerationResult generate(const std::vector<u8>& bytes, u32 seed=26) {
    auto file=loadFile(bytes);validate(file);VM vm(file,{seed, 0, 0, {}});vm.run();return vm.result();
}
void rejected(const std::string& body,const std::string& message) {
    const auto result=compileSource("bad-list-method.ips",[&](std::string_view){return "asset t{size 1x1 "+body+"}";});
    CHECK_FALSE(result.succeeded());CHECK(result.bytes.empty());REQUIRE_FALSE(result.diagnostics.empty());
    INFO(result.diagnostics[0].message);CHECK(result.diagnostics[0].message.find(message)!=std::string::npos);
    CHECK(result.diagnostics[0].location.has_value());
}
}

TEST_CASE("list methods retain stable first occurrences and never mutate aliases", "[list-methods]") {
    const std::string source=R"(asset t{size 1x1 output list point values output point state
      make()->list<i32>{pixel(0,0,#112233)x=unit();return [3,1,3,2]}
      echo(xs:list<i32>)->list<i32>{return xs.distinct().sort()}
      draw{original=make();alias=original;unique=original.distinct();ordered=echo(original)
        original.sort();original.distinct()
        for i in 0..list.len(original){values={original[i],alias[i]}}
        for i in 0..list.len(unique){values={unique[i],ordered[i]}}
        values={list.len(make().sort().distinct()),0};state={seed&65535,seed>>16}}
    })";
    const std::array<OutputPoint,8> expected{{{3,3},{1,1},{3,3},{2,2},{3,1},{1,2},{2,3},{3,0}}};
    for(const bool optimize:{false,true}) {
        const auto bytes=compile(source,optimize);const auto result=generate(bytes);
        REQUIRE(result.outputs[0].points.size()==expected.size());
        for(std::size_t i=0;i<expected.size();++i){CHECK(result.outputs[0].points[i].x==expected[i].x);CHECK(result.outputs[0].points[i].y==expected[i].y);}
        const auto reference=generate(compile("asset t{size 1x1 output point state draw{pixel(0,0,#112233)x=unit();y=unit();state={seed&65535,seed>>16}}}"));
        CHECK(result.outputs[1].points[0].x==reference.outputs[0].points[0].x);
        CHECK(result.outputs[1].points[0].y==reference.outputs[0].points[0].y);
        const auto restored=generate(assemble(disassemble(bytes)));CHECK(restored.image.pixels==result.image.pixels);
        REQUIRE(restored.outputs[0].points.size()==expected.size());
        for(std::size_t i=0;i<expected.size();++i){CHECK(restored.outputs[0].points[i].x==expected[i].x);CHECK(restored.outputs[0].points[i].y==expected[i].y);}
    }
}

TEST_CASE("list numerical ordering is exact and float exceptional classes preserve bits", "[list-methods]") {
    const auto integers=list(ValueType::I32,{i32{16777218},i32{16777217},std::numeric_limits<i32>::max(),std::numeric_limits<i32>::min(),i32{0}});
    const auto ordered=transformed(integers,true);
    const std::array pins{std::numeric_limits<i32>::min(),i32{0},i32{16777217},i32{16777218},std::numeric_limits<i32>::max()};
    for(std::size_t i=0;i<pins.size();++i)CHECK(std::get<i32>(ordered.data->elements[i])==pins[i]);
    const std::array<u32,10> original{0x7fc01234,0x80000000,0x3f800000,0xff800000,0,0x7f800000,0xffc05678,1,0x80000001,0x3f800000};
    std::vector<Value> values;for(const auto bits:original)values.push_back(std::bit_cast<f32>(bits));
    const auto floats=list(ValueType::F32,values);const auto unique=transformed(floats,false),sorted=transformed(floats,true);
    const std::array<u32,7> distinctPins{0x7fc01234,0x80000000,0x3f800000,0xff800000,0x7f800000,1,0x80000001};
    const std::array<u32,10> sortedPins{0xff800000,0x80000001,0x80000000,0,1,0x3f800000,0x3f800000,0x7f800000,0x7fc01234,0xffc05678};
    REQUIRE(unique.data->elements.size()==distinctPins.size());
    for(std::size_t i=0;i<distinctPins.size();++i)CHECK(std::bit_cast<u32>(std::get<f32>(unique.data->elements[i]))==distinctPins[i]);
    for(std::size_t i=0;i<sortedPins.size();++i)CHECK(std::bit_cast<u32>(std::get<f32>(sorted.data->elements[i]))==sortedPins[i]);
    for(std::size_t i=0;i<original.size();++i)CHECK(std::bit_cast<u32>(std::get<f32>(floats.data->elements[i]))==original[i]);
}

TEST_CASE("list colors include alpha and vectors sort lexicographically", "[list-methods]") {
    const std::vector<Value> colors{Color{1,2,3,255},Color{1,2,3,0},Color{0,255,255,255},Color{1,2,3,255}};
    const auto source=list(ValueType::Color,colors);const auto unique=transformed(source,false),sorted=transformed(source,true);
    CHECK(unique.data->elements.size()==3);CHECK(std::get<Color>(unique.data->elements[0])==Color{1,2,3,255});
    const std::array<Color,4> pins{{{0,255,255,255},{1,2,3,0},{1,2,3,255},{1,2,3,255}}};
    for(std::size_t i=0;i<pins.size();++i)CHECK(std::get<Color>(sorted.data->elements[i])==pins[i]);
    const auto vectors=list(ValueType::Vec2,{Vec2{1,3},Vec2{-1,5},Vec2{1,2},Vec2{1,3}});
    const auto result=transformed(vectors,true);const std::array<Vec2,4> vp{{{-1,5},{1,2},{1,3},{1,3}}};
    for(std::size_t i=0;i<vp.size();++i)CHECK(std::get<Vec2>(result.data->elements[i])==vp[i]);
    CHECK(transformed(vectors,false).data->elements.size()==3);
}

TEST_CASE("distinct description snapshots compare contents without draws or buffer copies", "[list-methods]") {
    Distribution a;a.family=DistributionFamily::Weighted;
    a.points=std::make_shared<PointDistributionData>(PointDistributionData{a.family,{{i32{16777217},i32{1}},{i32{16777218},i32{2}}}});
    Distribution b=a;b.points=std::make_shared<PointDistributionData>(*a.points);
    Distribution c=b;auto modified=std::make_shared<PointDistributionData>(*b.points);modified->points[0].value=i32{2};c.points=modified;
    const auto result=transformed(list(ValueType::Distribution,{a,b,c}),false);
    REQUIRE(result.data->elements.size()==2);CHECK(std::get<Distribution>(result.data->elements[0]).points==a.points);
    CHECK(std::get<Distribution>(result.data->elements[1]).points==c.points);CHECK(a.points!=b.points);
    auto data=std::make_shared<SamplerData>();data->type=ValueType::sampler(ValueType::I32);data->lower=i32{1};data->upper=i32{2};data->mode=SamplerMode::With;data->distribution=a;
    auto same=std::make_shared<SamplerData>(*data);same->distribution=b;
    auto other=std::make_shared<SamplerData>(*data);other->mode=SamplerMode::Uniform;
    const auto samplers=transformed(list(data->type,{SamplerValue{data->type,data},SamplerValue{data->type,same},SamplerValue{data->type,other}}),false);
    REQUIRE(samplers.data->elements.size()==2);CHECK(std::get<SamplerValue>(samplers.data->elements[0]).data==data);
    CHECK(std::get<SamplerValue>(samplers.data->elements[1]).data==other);
}

TEST_CASE("list methods share unchanged data and preflight fixed fuel and actual result quota", "[list-methods]") {
    const auto file=loadFile(compile("asset t{size 1x1 draw{items=[3,1,3,2]copy=items.distinct().sort()}}"));
    const auto descriptor=static_cast<u16>(std::find(file.collections.types.begin(),file.collections.types.end(),ValueType::list(ValueType::I32))-file.collections.types.begin());
    const auto input=list(ValueType::I32,{i32{3},i32{1},i32{3},i32{2}});const auto work=checkListSelection(input,false).work;CHECK(work==14);
    detail::Execution ex(file);ex.frames.emplace_back();ex.fuel=100;ex.frame().stream=26;ex.push(input);
    REQUIRE(detail::stepCollections(ex,{Opcode::ListDistinct,0,descriptor}));const auto result=std::get<ListValue>(ex.pop());
    CHECK(ex.fuel==86);CHECK(ex.resources.bytes==ResourceArena::listBytes(3));CHECK(ex.frame().stream==26);
    const auto stable=list(ValueType::I32,{i32{1},i32{2},i32{3}});ex.push(stable);
    REQUIRE(detail::stepCollections(ex,{Opcode::ListSort,0,descriptor}));CHECK(std::get<ListValue>(ex.pop()).data==stable.data);
    CHECK(ex.resources.bytes==ResourceArena::listBytes(3));
    for(const bool fuel:{false,true}){
        detail::Execution limited(file);limited.frames.emplace_back();limited.fuel=fuel?work-1:100;limited.push(input);
        if(!fuel)limited.resources.bytes=kMaxGenerationData-ResourceArena::listBytes(3)+1;
        CHECK_THROWS_AS(detail::stepCollections(limited,{Opcode::ListDistinct,0,descriptor}),Trap);CHECK(limited.resources.lists.empty());
    }
    for(const auto item:kListElementTypes){const auto empty=list(item,{});CHECK(transformed(empty,false).data==empty.data);if(isSortableList(empty.type))CHECK(transformed(empty,true).data==empty.data);}
    std::vector<Value> maximum;for(i32 i=255;i>=0;--i)maximum.push_back(i);
    const auto sorted=transformed(list(ValueType::I32,maximum),true);REQUIRE(sorted.data->elements.size()==256);
    for(i32 i=0;i<256;++i)CHECK(std::get<i32>(sorted.data->elements[static_cast<std::size_t>(i)])==i);
}

TEST_CASE("list methods support typed empty constants forward dependencies and formatting", "[list-methods]") {
    const std::string source=R"(asset t{size 1x1
      const list<i32> empty=[].distinct().sort()
      const list<i32> sorted=original.distinct().sort()
      const list<i32> original=[3,1,3,2]
      const list<distribution> descriptions=[gaussian(0,1),gaussian(0,1)].distinct()
      const list<sampler<i32>> dice=[sampler(1 to 2),sampler(1 to 2)].distinct()
      const list<color> colors=[#ff0000,#00ff00].sort()
      const list<vec2> points=[{2,1},{1,2},{1,1}].sort()
      output point p
      sort(x:i32)->i32{return x} distinct(x:i32)->i32{return x}
      draw{p={sorted[0],list.len(descriptions)+list.len(dice)} x=sort(distinct(1))}
    })";
    const auto formatted=ips::formatProgram(ips::parse(source));CHECK(ips::formatProgram(ips::parse(formatted))==formatted);
    for(const bool optimize:{false,true}){const auto a=generate(compile(source,optimize)),b=generate(compile(formatted,optimize));
        CHECK(a.outputs[0].points[0].x==1);CHECK(a.outputs[0].points[0].y==2);CHECK(a.image.pixels==b.image.pixels);}
}

TEST_CASE("list method invalid receivers arguments unsupported sorts and unset values trap", "[list-methods]") {
    rejected("draw{x=(1).distinct()}","list<T>");
    rejected("draw{x=[1].distinct(1)}","takes no arguments");
    rejected("draw{x=[1].sort(descending=true)}","takes no arguments");
    rejected("draw{x=[gaussian(0,1)].sort()}","does not support");
    rejected("draw{x=[sampler(1)].sort()}","does not support");
    rejected("draw{x=[].sort()}","empty list");
    for(const auto& body:{"if(false){items=[1]}items.distinct()","if(false){items=[1]}items.sort()",
        "if(false){dice=sampler(1)}items=[dice].distinct()","if(false){d=gaussian(0,1)}items=[d].distinct()"}){
        const auto file=loadFile(compile("asset t{size 1x1 draw{"+std::string(body)+"}}"));VM vm(file,{});CHECK_THROWS_AS(vm.run(),Trap);CHECK_THROWS_AS(vm.result(),std::logic_error);
    }
    for(const auto& dead:{"list.distinct 65535","list.sort 65535"})
        CHECK_THROWS_AS(assemble("meta: size=1x1\nfn draw ( ) -> ( ):\nreturn\n"+std::string(dead)+"\nendfn\n"),std::exception);
}
