#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_exception.hpp>
#include "support/geometry_fixture.hpp"
#include "iprop/DynamicParts.hpp"
#include "iprop/VM.hpp"
#include "Compiler.hpp"
#include "ipa.hpp"
#include "ipdis.hpp"
#include "ips/Parser.hpp"
#include "ips/Printer.hpp"
#include "ips/Semantics.hpp"

using namespace iprop;
namespace {
std::vector<u8> compile(const std::string& text,bool optimize=true) {
    const auto p=ips::parse(text);ips::checkAsset(p,{});return compileProgram(p,{},optimize);
}
}
TEST_CASE("dynamic instances share sources and use a lifetime independent creation counter","[dynamic-parts-v09]") {
    LoadedFile f;f.code.functions.emplace_back();detail::Execution ex(f);ex.fuel=1000000000;
    {const Regions2dValue source{geometry_fixture::holes(ex)};const auto bytes=ex.resources.bytes;
    {const auto p=detail::createPart(ex,source,0);const auto q=detail::createPart(ex,source,0);
        CHECK(p.data->id==0);CHECK(q.data->id==1);CHECK(p.data->sourceId==9);
        CHECK(std::get<RegionSource>(p.data->source).geometry==source.data);
        CHECK(p.data->center==source.data->regions[0].center);
        CHECK(ex.resources.bytes==bytes+512);
        const auto ps=detail::partsFromRegions(ex,source);REQUIRE(ps.data->parts.size()==2);
        CHECK(ps.data->parts[0].data->id==2);CHECK(ps.data->parts[1].data->id==3);
        CHECK(ps.data->parts[1].data->sourceId==10);}
    CHECK(ex.resources.bytes==bytes);CHECK(ex.dynamicPartCreations==4);
    for(u32 i=4;i<256;++i)CHECK(detail::createPart(ex,source,0).data->id==static_cast<i32>(i));
    CHECK(ex.resources.bytes==bytes);CHECK_THROWS_WITH(detail::createPart(ex,source,0),"dynamic part creations exceed 256");
    CHECK_THROWS(detail::partsFromRegions(ex,source));CHECK(ex.resources.bytes==bytes);
    CHECK_THROWS(detail::createPart(ex,source,-1));}
    CHECK(ex.resources.bytes==0);
    CHECK_THROWS(detail::createPart(ex,Regions2dValue{},0));
    {detail::GeometryBuilder b(ex,GeometryOrigin::Contours);const Regions2dValue empty{std::move(b).finish()};
        CHECK(detail::partsFromRegions(ex,empty).data->parts.empty());CHECK(ex.dynamicPartCreations==256);}
    CHECK(ex.resources.bytes==0);
}
TEST_CASE("dynamic raster capture retains soft alpha and validates empty sources before publication","[dynamic-parts-v09]") {
    LoadedFile f;f.code.functions.emplace_back();detail::Execution ex(f);ex.fuel=1000000;
    ex.target=RenderTarget::create(5,4,{1,1});
    auto buffer=std::make_shared<SurfaceBuffer>();buffer->format=SurfaceBuffer::Format::A8;
    buffer->raster.width=5;buffer->raster.height=4;buffer->coverage.resize(20);
    MaskValue source{buffer,{{},{-3,-2}}};
    CHECK_THROWS_WITH(detail::createPart(ex,source),"empty dynamic part");CHECK(ex.dynamicPartCreations==0);
    buffer->coverage[6]=1;buffer->coverage[13]=127;
    {const auto p=detail::createPart(ex,source);CHECK(p.data->id==0);CHECK(p.data->sourceId==-1);
        CHECK(p.data->boundsOrigin==Vec2{-2,-1});CHECK(p.data->boundsSize==Vec2{3,2});
        CHECK(p.data->center==Vec2{-0.5f,0});CHECK(std::get<MaskValue>(p.data->source).buffer==buffer);}
    CHECK(ex.resources.bytes==0);CHECK(ex.dynamicPartCreations==1);
    auto rgba=std::make_shared<SurfaceBuffer>();rgba->raster.resize(5,4);rgba->raster.at(2,1)={7,8,9,128};
    {const auto p=detail::createPart(ex,LayerValue{rgba,{}});CHECK(p.data->id==1);
        CHECK(p.data->center==Vec2{2.5f,1.5f});CHECK(std::get<LayerValue>(p.data->source).buffer==rgba);}
    CHECK(ex.resources.bytes==0);
}
TEST_CASE("dynamic creation frontend transfers handles and roundtrips without drawing or RNG","[dynamic-parts-v09]") {
    const std::string text=R"(asset t{size 8x4 output list point trace
        pass(p:part2d)->part2d{return p}
        many(ps:parts2d)->parts2d{return ps}
        draw{rs=regions(voronoi({0,0},{8,4},[{2,2},{6,2}]))
            p=pass(part.create(index=1,source=rs)) ps=many(parts.from_regions(rs))
            trace={part.id(p),part.source_id(p)} trace={parts.count(ps),part.id(parts.get(index=1,source=ps))}
            layer image(alpha=surface.source_over){pixel(3,1,#ff000080)}
            trace={part.id(part.create(mask.from(image))),part.id(part.create(image))}
            trace={seed,seed}
        }})";
    for(const bool optimize:{false,true}){
        const auto bytes=compile(text,optimize);const auto f=loadFile(bytes);validate(f);GenerationInputs in;in.seed=7;
        VM vm(f,in);vm.run();const auto result=vm.result();const auto& p=result.outputs[0].points;
        REQUIRE(p.size()==4);CHECK(p[0].x==0);CHECK(p[0].y==1);CHECK(p[1].x==2);CHECK(p[1].y==2);
        CHECK(p[2].x==3);CHECK(p[2].y==4);CHECK(p[3].x==7);CHECK(p[3].y==7);
        for(const auto pixel:vm.canvas().pixels)CHECK(pixel.a==0);
        const auto assembly=disassemble(bytes);CHECK(disassemble(assemble(assembly))==assembly);
        CHECK(disassemble(compile(ips::formatProgram(ips::parse(text)),optimize))==assembly);
        for(const auto bad:{"parts.get(ps,-1)","parts.get(ps,2)"}){
            const auto invalid=loadFile(compile("asset t{size 8x4 draw{ps=parts.from_regions(regions(voronoi({0,0},{8,4},2,0))) "+std::string(bad)+"}}",optimize));
            VM broken(invalid,{});CHECK_THROWS(broken.run());
        }
    }
    CHECK_THROWS(compile("asset t{size 1x1 draw{part.create(1)}}"));
    CHECK_THROWS(compile("asset t{size 1x1 const p=part.create(1) draw{}}"));
}
