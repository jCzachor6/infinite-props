#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_exception.hpp>
#include <cmath>
#include <limits>
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
    ex.target=RenderTarget::create(16,16,{1,1});
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
TEST_CASE("dynamic appearance and placement views are independent and preserve identity","[dynamic-parts-v09]") {
    LoadedFile f;f.code.functions.emplace_back();detail::Execution ex(f);ex.fuel=10000000;
    ex.target=RenderTarget::create(32,32,{1,1});
    {const Regions2dValue source{geometry_fixture::holes(ex)};const auto original=detail::createPart(ex,source,0);
    const auto filled=detail::fillPart(ex,original,{17,23,29,128});
    auto image=std::make_shared<SurfaceBuffer>();image->raster.resize(2,3);
    const auto textured=detail::texturePart(ex,filled,LayerValue{image,{{},{-1,2}}});
    const auto filledAgain=detail::fillPart(ex,textured,{31,37,41,1});
    CHECK_FALSE(original.data->fill);CHECK_FALSE(original.data->texture);
    CHECK(filled.data->fill==Color{17,23,29,128});CHECK_FALSE(filled.data->texture);
    CHECK_FALSE(textured.data->fill);CHECK(textured.data->texture->buffer==image);
    CHECK(filledAgain.data->fill==Color{31,37,41,1});CHECK_FALSE(filledAgain.data->texture);
    for(const auto& p:{original,filled,textured,filledAgain}){
        CHECK(p.data->id==0);CHECK(p.data->sourceId==9);CHECK(std::get<RegionSource>(p.data->source).geometry==source.data);
    }
    for(i32 rot:{0,90,180,270})for(i32 mx:{0,1})for(i32 my:{0,1}){
        const auto placed=detail::placePart(ex,filled,{-2.25f,3.5f},rot,mx,my);
        const auto replacement=detail::placePart(ex,placed,{-2.25f,3.5f},rot,mx,my);
        CHECK(placed.data->center==replacement.data->center);CHECK(placed.data->translation==replacement.data->translation);
        CHECK(placed.data->boundsOrigin==replacement.data->boundsOrigin);CHECK(placed.data->boundsSize==replacement.data->boundsSize);
        CHECK(placed.data->fill==filled.data->fill);CHECK(placed.data->id==0);
        CHECK(placed.data->boundsSize==(rot==90||rot==270?Vec2{8,10}:Vec2{10,8}));
        CHECK(std::abs(placed.data->center.x+2.25f)<=0.5f);CHECK(std::abs(placed.data->center.y-3.5f)<=0.5f);
    }
    CHECK(original.data->center==source.data->regions[0].center);CHECK(ex.dynamicPartCreations==1);
    const auto bytes=ex.resources.bytes;
    for(const auto args:{std::array<i32,3>{45,0,0},{-90,0,0},{360,0,0},{0,2,0},{0,0,-1}})
        CHECK_THROWS(detail::placePart(ex,original,{0,0},args[0],args[1],args[2]));
    CHECK_THROWS(detail::placePart(ex,original,{std::numeric_limits<f32>::infinity(),0},0,0,0));
    CHECK_THROWS(detail::placePart(ex,original,{std::numeric_limits<f32>::max(),0},0,0,0));
    CHECK(ex.resources.bytes==bytes);}
    CHECK(ex.resources.bytes==0);
}
TEST_CASE("raster placement keeps exact pixel anchors and composes existing source D4","[dynamic-parts-v09]") {
    LoadedFile f;f.code.functions.emplace_back();detail::Execution ex(f);ex.fuel=1000000;
    ex.target=RenderTarget::create(24,24,{3,3});
    auto buffer=std::make_shared<SurfaceBuffer>();buffer->raster.resize(2,3);
    buffer->raster.scaleX=buffer->raster.scaleY=3;buffer->raster.at(0,1)={1,2,3,1};
    {const auto original=detail::createPart(ex,LayerValue{buffer,{}});
    CHECK(original.data->original.centerX==0.5);CHECK(original.data->original.centerY==1.5);
    const auto p=detail::placePart(ex,original,{0,0},0,0,0);
    CHECK(p.data->translation==RasterPoint{0,-1});CHECK(p.data->center==Vec2{1.f/6,1.f/6});
    const auto n=detail::placePart(ex,original,{-0.5f,-0.5f},0,0,0);
    CHECK(n.data->translation==RasterPoint{-2,-3});CHECK(n.data->center==Vec2{-0.5f,-0.5f});
    for(u8 a=0;a<8;++a){const auto t=*D4Transform::fromKind(a);
        const auto captured=detail::createPart(ex,LayerValue{buffer,{t,{-5,-7}}});
        for(u8 b=0;b<8;++b){const auto next=*D4Transform::fromKind(b);
            DynamicPartData moved=*captured.data;moved.transform=next;moved.translation={11,13};
            const auto mapping=detail::partSurfaceMapping(ex,moved,*buffer,{t,{-5,-7}});
            CHECK(mapping.transform==t.compose(next));
            const auto old=t.mapPixel({0,1},RasterPoint{2,3});
            // Transform the edges of this source pixel, then take their min.
            const auto ax=next.mapPixelWide({old.x-5,old.y-7},{1,1});
            const auto bx=next.mapPixelWide({old.x-4,old.y-6},{1,1});
            const auto newPixel=mapping.transform.mapPixel({0,1},RasterPoint{2,3});
            CHECK(mapping.origin.x+newPixel.x==std::min(ax.x,bx.x)+11);
            CHECK(mapping.origin.y+newPixel.y==std::min(ax.y,bx.y)+13);
        }
    }}CHECK(ex.resources.bytes==0);
    ex.target=RenderTarget::create(24,24,{2,3});buffer->raster.scaleX=2;
    {const auto p=detail::createPart(ex,LayerValue{buffer,{}});
        CHECK_THROWS(detail::placePart(ex,p,{0,0},90,0,0));
        ex.target=RenderTarget::create(24,24,{3,2});CHECK_NOTHROW(detail::placePart(ex,p,{0,0},90,0,0));}
    CHECK(ex.resources.bytes==0);
}
TEST_CASE("dynamic view builtins compile roundtrip and expose effective cached metadata","[dynamic-parts-v09]") {
    const std::string text=R"(asset t{size 8x4 output list point trace
        pass(p:part2d)->part2d{return p}
        draw{rs=regions(voronoi({0,0},{8,4},[{2,2},{6,2}])) p=part.create(rs,0)
            layer image{rect(0,0,8,4,#abc)}
            q=pass(part.place(mirror_y=0,at={-0.5,0.5},p=part.texture(part.fill(p,#fff),image),mirror_x=0,rot=90))
            trace=part.center(p)trace=part.center(q)trace=part.bounds_origin(q)trace=part.bounds_size(q)
            trace={part.id(q),part.source_id(q)} trace={part.id(part.create(rs,0)),seed}
        }})";
    for(const bool optimize:{false,true}){
        const auto bytes=compile(text,optimize);const auto f=loadFile(bytes);validate(f);VM vm(f,{});vm.run();
        const auto result=vm.result();const auto& points=result.outputs[0].points;
        REQUIRE(points.size()==6);CHECK(points[0].x==2);CHECK(points[0].y==2);
        CHECK(points[1].x==0);CHECK(points[1].y==1);CHECK(points[2].x==-2);CHECK(points[2].y==-1);
        CHECK(points[3].x==4);CHECK(points[3].y==4);CHECK(points[4].x==0);CHECK(points[4].y==0);
        CHECK(points[5].x==1);CHECK(points[5].y==0);
        CHECK(disassemble(assemble(disassemble(bytes)))==disassemble(bytes));
        CHECK(disassemble(compile(ips::formatProgram(ips::parse(text)),optimize))==disassemble(bytes));
    }
}
