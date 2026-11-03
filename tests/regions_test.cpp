#include <catch2/catch_test_macros.hpp>
#include "iprop/Regions.hpp"
#include "iprop/Voronoi.hpp"
#include "support/geometry_fixture.hpp"
#include "support/contour_fixture.hpp"
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
