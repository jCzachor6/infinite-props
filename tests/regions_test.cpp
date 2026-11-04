#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include "iprop/Regions.hpp"
#include "iprop/Voronoi.hpp"
#include "support/geometry_fixture.hpp"
#include "support/contour_fixture.hpp"
#include "support/regions_fixture.hpp"
#include "iprop/VM.hpp"
#include "Compiler.hpp"
#include "ipa.hpp"
#include "ipdis.hpp"
#include "ips/Parser.hpp"
#include "ips/Printer.hpp"
#include "ips/Semantics.hpp"
#include <cmath>
#include <limits>

using namespace iprop;
namespace {
std::vector<u8> compile(const std::string& text,bool optimize=true) {
    const auto p=ips::parse(text);ips::checkAsset(p,{});return compileProgram(p,{},optimize);
}
}
TEST_CASE("regions conversion shares topology metadata holes and empty stores","[regions-v09]") {
    LoadedFile f;detail::Execution ex(f);ex.fuel=100000000;
    for (const auto make:{geometry_fixture::adjacent,geometry_fixture::holes,geometry_fixture::fractional}) {
        {const auto source=make(ex);const auto bytes=ex.resources.bytes,fuel=ex.fuel;
        for(u32 i=0;i<100;++i) {
            const auto rs=detail::geometryRegions(ex,source);CHECK(rs.get()==source.get());
            CHECK(ex.resources.bytes==bytes);CHECK(ex.fuel==fuel);
            for(u32 j=0;j<rs->regions.size();++j) {
                CHECK(rs->regions[j].sourceId==source->regions[j].sourceId);
                CHECK(rs->regions[j].area==source->regions[j].area);
                CHECK(rs->regions[j].center==source->regions[j].center);
            }
        }}CHECK(ex.resources.bytes==0);
    }
    {detail::GeometryBuilder b(ex,GeometryOrigin::Contours);const auto empty=std::move(b).finish();
    CHECK(detail::geometryRegions(ex,empty).get()==empty.get());CHECK(empty->regions.empty());}
    CHECK(ex.resources.bytes==0);
}
TEST_CASE("regions filter net area preserve source IDs reparent islands and remap neighbors","[regions-v09]") {
    LoadedFile f;detail::Execution ex(f);ex.fuel=100000000;
    {const auto source=geometry_fixture::holes(ex);const auto kept=detail::geometryRegions(ex,source,2);
    REQUIRE(kept->regions.size()==1);CHECK(kept->regions[0].sourceId==9);CHECK(kept->regions[0].area==76);
    CHECK(kept->holes.size()==1);CHECK(kept->source.get()==source.get());
    CHECK(detail::geometryRegions(ex,source,77)->regions.empty());
    CHECK(detail::geometryRegions(ex,source,1).get()==source.get());}
    {detail::GeometryBuilder b(ex,GeometryOrigin::Contours);
    const auto outer=geometry_fixture::rectangle(b,{0,0},{10,10});
    const auto hole=geometry_fixture::rectangle(b,{0.5f,0.5f},{9.5f,9.5f},outer,true);
    const auto island=geometry_fixture::rectangle(b,{2,2},{8,8},hole);
    const std::array<u32,1> holes{hole};b.region(outer,3,holes);b.region(island,5);
    const auto source=std::move(b).finish();const auto kept=detail::geometryRegions(ex,source,20);
    REQUIRE(kept->regions.size()==1);CHECK(kept->regions[0].sourceId==5);CHECK(kept->regions[0].area==36);
    CHECK(kept->rings[0].parent==kNoGeometryIndex);CHECK(kept->holes.empty());}
    {const std::array<Vec2,3> sites{{{0,1},{2,1},{7,1}}};
    const auto source=detail::explicitVoronoi(ex,{0,0},{10,2},sites,0);
    const auto rs=detail::geometryRegions(ex,source,5);
    REQUIRE(rs->regions.size()==2);CHECK(rs->regions[0].sourceId==1);CHECK(rs->regions[1].sourceId==2);
    REQUIRE(rs->regionNeighbors(0).size()==1);CHECK(rs->regionNeighbors(0)[0]==1);
    REQUIRE(rs->regionNeighbors(1).size()==1);CHECK(rs->regionNeighbors(1)[0]==0);
    for(const auto& e:rs->edges)CHECK(e.provenance.edge<source->edges.size());}
    CHECK(ex.resources.bytes==0);
    CHECK_THROWS(detail::geometryRegions(ex,{},0));
    {const auto source=geometry_fixture::adjacent(ex);
    for(const auto x:{-1.f,std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()})
        CHECK_THROWS(detail::geometryRegions(ex,source,x));}
    CHECK(ex.resources.bytes==0);
}
TEST_CASE("region metadata frontend roundtrips sharing helper names and checked indices","[regions-v09]") {
    const std::string text=R"(asset t{size 8x4 output list point trace
        pass(rs:regions2d)->regions2d{return rs}
        draw{d=voronoi({0,0},{8,4},[{2,2},{6,2}]) rs=pass(regions(source=d))
            trace={regions.count(rs),regions.id(rs,1)} trace=regions.center(rs,0)
            trace={regions.area(index=0,rs=rs),list.len(regions.neighbors(rs,0))}
            layer image{rect(0,0,8,4,#fff)} c=contour(mask.from(image),0.5)
            cr=regions(c) trace={regions.count(cr),regions.area(cr,0)}
        }})";
    for(const bool optimize:{false,true}) {
        const auto bytes=compile(text,optimize);const auto f=loadFile(bytes);validate(f);VM vm(f,{});vm.run();const auto result=vm.result();
        REQUIRE(result.outputs.size()==1);const auto& points=result.outputs[0].points;REQUIRE(points.size()==4);
        for(u32 i=0;i<4;++i){const std::array<Vec2,4> want{{{2,1},{2,2},{16,1},{1,32}}};CHECK(points[i].x==want[i].x);CHECK(points[i].y==want[i].y);}
        const auto assembly=disassemble(bytes);CHECK(disassemble(assemble(assembly))==assembly);
        const auto formatted=ips::formatProgram(ips::parse(text));CHECK_NOTHROW(compile(formatted,optimize));
        for(const auto body:{"regions.id(rs,-1)","regions.area(rs,2)","regions.center(rs,2)","regions.neighbors(rs,2)"}) {
            const auto bad=loadFile(compile("asset t{size 2x2 draw{rs=regions(voronoi({0,0},{2,2},1,0)) "+std::string(body)+"}}",optimize));
            VM invalid(bad,{});CHECK_THROWS(invalid.run());
        }
        for(const auto body:{"regions.count(rs)","regions.id(rs,0)","regions.area(rs,0)","regions.center(rs,0)","regions.neighbors(rs,0)"}) {
            const auto bad=loadFile(compile("asset t{size 2x2 draw{if(seed==0){rs=regions(voronoi({0,0},{2,2},1,0))} "+std::string(body)+"}}",optimize));
            GenerationInputs inputs;inputs.seed=1;VM invalid(bad,inputs);CHECK_THROWS(invalid.run());
        }
    }
    CHECK_NOTHROW(compile("asset t{size 1x1 regions(x:i32)->i32{return x} draw{pixel(0,0,rgba(regions(7),0,0,255))}}"));
    CHECK_THROWS(compile("asset t{size 1x1 const a=regions(voronoi({0,0},{1,1},1,0)) draw{}}"));
    CHECK_THROWS(compile("asset t{size 1x1 draw{regions(1)}}"));
}

TEST_CASE("region offsets shrink vanish grow round holes and keep independent sources","[regions-v09]") {
    LoadedFile f;detail::Execution ex(f);ex.fuel=1000000000;
    {const auto source=geometry_fixture::adjacent(ex);const auto before=ex.resources.bytes;
    {const auto rs=detail::offsetRegions(ex,source,0.25f);REQUIRE(rs->regions.size()==2);
    CHECK(rs->regions[0].area==2.25f);CHECK(rs->regions[0].center==Vec2{1,1});CHECK(rs->regions[1].center==Vec2{3,1});
    CHECK(rs->source.get()==source.get());CHECK(rs->regionNeighbors(0).size()==1);CHECK(rs->regionNeighbors(1).size()==1);
    CHECK(rs->rings[0].count==4);CHECK(rs->rings[1].count==4);}
    CHECK(ex.resources.bytes==before);CHECK(detail::offsetRegions(ex,source,1)->regions.empty());CHECK(detail::offsetRegions(ex,source,2)->regions.empty());
    const auto grown=detail::offsetRegions(ex,source,-1);REQUIRE(grown->regions.size()==2);CHECK(grown->regions[0].sourceId==0);CHECK(grown->regions[1].sourceId==1);
    CHECK(grown->regions[0].area>15);CHECK(grown->regions[0].area<16);CHECK(grown->rings[0].count>4);
    const auto twice=detail::offsetRegions(ex,grown,0.25f);CHECK(twice->source.get()==source.get());}
    CHECK(ex.resources.bytes==0);
    {const auto source=geometry_fixture::holes(ex);const auto shrink=detail::offsetRegions(ex,source,0.25f);
    REQUIRE(shrink->regions.size()==2);CHECK(shrink->regions[0].sourceId==9);CHECK(shrink->regions[1].sourceId==10);
    CHECK(shrink->holes.size()==1);CHECK(shrink->regions[1].area==0.25f);
    const auto grown=detail::offsetRegions(ex,source,-1.01f);REQUIRE(grown->regions.size()==2);CHECK(grown->holes.empty());
    CHECK(source->holes.size()==1);CHECK(source->regions[0].area==76);}
    CHECK(ex.resources.bytes==0);
}
TEST_CASE("region masks retain holes off-target scale and geometric union coverage","[regions-v09]") {
    LoadedFile f;detail::Execution ex(f);ex.fuel=1000000000;ex.target=RenderTarget::create(1,1,{1,1});
    {const auto source=geometry_fixture::adjacent(ex);const auto before=ex.resources.bytes;
    {const auto mask=detail::rasterRegions(ex,*source);CHECK(mask.buffer->raster.width==4);CHECK(mask.buffer->raster.height==2);
    CHECK(mask.mapping.origin.x==0);CHECK(mask.mapping.origin.y==0);for(const auto a:mask.buffer->coverage)CHECK(a==255);}
    CHECK(ex.resources.bytes==before);
    const auto grown=detail::offsetRegions(ex,source,-0.5f);const auto mask=detail::rasterRegions(ex,*grown);
    CHECK(mask.mapping.origin.x==-1);CHECK(mask.mapping.origin.y==-1);CHECK(maskCoverage(mask,2,1)==255);}
    CHECK(ex.resources.bytes==0);
    {const auto source=geometry_fixture::holes(ex);const auto only=detail::rasterRegions(ex,*source,0),all=detail::rasterRegions(ex,*source);
    CHECK(maskCoverage(only,2,2)==0);CHECK(maskCoverage(all,2,2)>0);CHECK(maskCoverage(all,0,0)==255);
    CHECK(maskCoverage(all,10,0)==0);
    ex.target.canvas().scaleX=2;ex.target.canvas().scaleY=4;
    const auto scaled=detail::rasterRegions(ex,*source);CHECK(scaled.buffer->raster.width==20);CHECK(scaled.buffer->raster.height==32);
    CHECK(maskCoverage(scaled,5,9)==0);CHECK(scaled.buffer->raster.scaleX==2);CHECK(scaled.buffer->raster.scaleY==4);}
    CHECK(ex.resources.bytes==0);
    {detail::GeometryBuilder b(ex,GeometryOrigin::Contours);const auto empty=std::move(b).finish();const auto m=detail::rasterRegions(ex,*empty);
    REQUIRE(m.buffer->coverage.size()==1);CHECK(m.buffer->coverage[0]==0);CHECK(m.mapping.origin.x==0);CHECK(m.mapping.origin.y==0);}
    CHECK(ex.resources.bytes==0);
}

TEST_CASE("region offsets split narrow bridges and retain fragment IDs and ordering","[regions-v09]") {
    LoadedFile f;detail::Execution ex(f);ex.fuel=1000000000;
    const std::array<Vec2,12> dumb{{{0,0},{4,0},{4,1.5f},{8,1.5f},{8,0},{12,0},{12,4},{8,4},{8,2.5f},{4,2.5f},{4,4},{0,4}}};
    const std::array<test_support::RegionRingInput,1> input{{{dumb,kNoGeometryIndex,7}}};
    {const auto source=test_support::regionFixture(ex,input);const auto rs=detail::offsetRegions(ex,source,0.6f);
    REQUIRE(rs->regions.size()==2);CHECK(rs->regions[0].sourceId==7);CHECK(rs->regions[1].sourceId==7);
    CHECK(rs->regions[0].boundsOrigin.x<rs->regions[1].boundsOrigin.x);CHECK(std::abs(rs->regions[0].area-rs->regions[1].area)<0.0001f);
    CHECK(rs->regionNeighbors(0).empty());CHECK(rs->regionNeighbors(1).empty());
    CHECK(detail::offsetRegions(ex,source,0.6f,10)->regions.empty());
    CHECK(detail::offsetRegions(ex,source,0.25f)->regions.size()==1);
    CHECK(source->regions.size()==1);CHECK(source->regions[0].area==36);}
    CHECK(ex.resources.bytes==0);
}
TEST_CASE("region offset exhaustive binary masks are closed bounded and release their scratch","[regions-v09]") {
    LoadedFile f;detail::Execution ex(f);ex.fuel=1000000000000ull;
    for(u32 bits=0;bits<512;++bits){
        const auto before=ex.resources.bytes;
        {std::array<u8,9> p{};for(u32 i=0;i<9;++i)p[i]=(bits&(1u<<i))?255:0;
        const auto mask=test_support::contourMask(ex,3,3,p);
        const auto source=detail::maskContours(ex,mask,{});
        for(const f32 inset:{0.125f,-0.25f,0.75f}){
            INFO(bits << " inset " << inset);const auto rs=detail::offsetRegions(ex,source,inset);
            for(const auto& r:rs->regions){CHECK(r.area>0);CHECK(r.sourceId>=0);CHECK(static_cast<std::size_t>(r.sourceId)<source->regions.size());}
            for(const auto& r:rs->rings){
                CHECK(r.count>=3);CHECK(r.count<=2048);CHECK((r.area<0)==r.hole);
                for(u32 k=0;k<r.count;++k){const auto use=rs->uses[r.begin+k];CHECK(rs->end(use)==rs->start(rs->uses[r.begin+(k+1)%r.count]));}
            }
        }}CHECK(ex.resources.bytes==before);
    }
}

TEST_CASE("regions points rejects holes and 257 vertices without changing full geometry","[regions-v09]") {
    LoadedFile f;f.code.functions.emplace_back();
    for(const u32 n:{256u,257u,2048u}){
        detail::Execution ex(f);ex.fuel=100000000;ex.target=RenderTarget::create(1,1,{1,1});ex.frames.emplace_back();ex.frames.back().stack.reserve(4);
        std::vector<Vec2> polygon;for(u32 i=0;i<n;++i){const double a=6.283185307179586476925286766559*i/n;polygon.push_back({static_cast<f32>(16+12*std::cos(a)),static_cast<f32>(16+12*std::sin(a))});}
        const std::array<test_support::RegionRingInput,1> input{{{polygon,kNoGeometryIndex,0}}};const auto source=test_support::regionFixture(ex,input);const auto before=ex.resources.bytes;
        ex.push(Regions2dValue{source});ex.push(i32{0});
        if(n==256){CHECK(detail::stepRegions(ex,{Opcode::RegionsPoints,0,0}));const auto list=ex.popHandle<ListValue>("points");REQUIRE(list.data->elements.size()==256);
            for(u32 i=0;i<n;++i)CHECK(std::get<Vec2>(list.data->elements[i])==polygon[i]);}
        else{CHECK_THROWS(detail::stepRegions(ex,{Opcode::RegionsPoints,0,0}));CHECK(ex.resources.bytes==before);}
        CHECK(source->rings[0].count==n);const auto mask=detail::rasterRegions(ex,*source);CHECK(maskCoverage(mask,16,16)==255);
    }
    {detail::Execution ex(f);ex.fuel=100000000;ex.target=RenderTarget::create(1,1,{1,1});ex.frames.emplace_back();
    const auto source=geometry_fixture::holes(ex);const auto before=ex.resources.bytes;ex.push(Regions2dValue{source});ex.push(i32{0});
    CHECK_THROWS(detail::stepRegions(ex,{Opcode::RegionsPoints,0,0}));CHECK(ex.resources.bytes==before);CHECK(source->rings.size()==3);CHECK(source->holes.size()==1);
    CHECK(maskCoverage(detail::rasterRegions(ex,*source,0),2,2)==0);}
}
TEST_CASE("region limits apply after fragment filtering and bound discarded intermediates","[regions-v09]") {
    LoadedFile f;detail::Execution ex(f);ex.fuel=10000000000ull;
    // Isolate geometry/quota boundaries from the independent work guard.
    ExecutionLimits limits;limits.workLimit=10000000000ull;
    ex.workBudget=std::make_shared<detail::WorkBudget>(limits);ex.resources.workBudget=ex.workBudget;
    std::vector<Vec2> comb;
    for(u32 i=0;i<257;++i){const auto x=static_cast<f32>(i*4);comb.push_back({x,0});comb.push_back({x+3,0});if(i<256){comb.push_back({x+3,5});comb.push_back({x+4,5});}}
    comb.push_back({1027,6});comb.push_back({0,6});
    const std::array<test_support::RegionRingInput,1> input{{{comb,kNoGeometryIndex,0}}};
    {const auto source=test_support::regionFixture(ex,input);const auto before=ex.resources.bytes;
    CHECK_THROWS_WITH(detail::offsetRegions(ex,source,0.6f),Catch::Matchers::ContainsSubstring("256 regions after min_area"));CHECK(ex.resources.bytes==before);
    CHECK(detail::offsetRegions(ex,source,0.6f,10)->regions.empty());CHECK(ex.resources.bytes==before);}
    CHECK(ex.resources.bytes==0);
    {const auto source=geometry_fixture::adjacent(ex);const auto before=ex.resources.bytes;
    CHECK_THROWS_WITH(detail::offsetRegions(ex,source,-1e10f,1e30f),Catch::Matchers::ContainsSubstring("65536 intermediate segments"));CHECK(ex.resources.bytes==before);}
    CHECK(ex.resources.bytes==0);
    {const auto source=geometry_fixture::adjacent(ex);const auto before=ex.resources.bytes;
    CHECK_THROWS_WITH(detail::offsetRegions(ex,source,-5000,1e30f),Catch::Matchers::ContainsSubstring("2048 vertices"));CHECK(ex.resources.bytes==before);}
    CHECK(ex.resources.bytes==0);
}
TEST_CASE("region masks and changed geometry survive execution destruction","[regions-v09]") {
    LoadedFile f;std::shared_ptr<const GeometryData> retained;MaskValue image;
    {detail::Execution ex(f);ex.fuel=100000000;ex.target=RenderTarget::create(1,1,{2,4});
    const auto source=geometry_fixture::holes(ex);retained=detail::offsetRegions(ex,source,0.25f);image=detail::rasterRegions(ex,*retained);}
    REQUIRE(retained->regions.size()==2);CHECK(retained->source->regions.size()==2);CHECK(retained->regions[1].area==0.25f);
    CHECK(maskCoverage(image,1,2)==255);retained.reset();CHECK(maskCoverage(image,1,2)==255);image={};
}
