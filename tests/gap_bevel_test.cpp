#include <catch2/catch_test_macros.hpp>
#include "support/geometry_fixture.hpp"
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
void sameGeometry(const GeometryData& a,const GeometryData& b) {
    REQUIRE(a.points.size()==b.points.size());
    for(u32 i=0;i<a.points.size();++i)CHECK(a.points[i]==b.points[i]);
    REQUIRE(a.edges.size()==b.edges.size());
    for(u32 i=0;i<a.edges.size();++i){const auto x=a.edges[i],y=b.edges[i];
        CHECK(x.first==y.first);CHECK(x.second==y.second);
        CHECK(x.provenance.edge==y.provenance.edge);CHECK(x.provenance.source==y.provenance.source);
        CHECK(x.provenance.neighbor==y.provenance.neighbor);CHECK(x.provenance.origin==y.provenance.origin);}
    REQUIRE(a.rings.size()==b.rings.size());
    for(u32 i=0;i<a.rings.size();++i){const auto x=a.rings[i],y=b.rings[i];
        CHECK(x.begin==y.begin);CHECK(x.count==y.count);CHECK(x.parent==y.parent);CHECK(x.hole==y.hole);
        CHECK(x.area==y.area);CHECK(x.momentX==y.momentX);CHECK(x.momentY==y.momentY);}
    REQUIRE(a.regions.size()==b.regions.size());
    for(u32 i=0;i<a.regions.size();++i){const auto x=a.regions[i],y=b.regions[i];
        CHECK(x.outer==y.outer);CHECK(x.sourceId==y.sourceId);CHECK(x.area==y.area);CHECK(x.center==y.center);
        CHECK(x.holesCount==y.holesCount);CHECK(x.neighborsCount==y.neighborsCount);}
}
}
TEST_CASE("constant gaps are the identical offset including holes vanishing and repeated derivation","[gap-bevel-v09]") {
    LoadedFile f;f.code.functions.emplace_back();detail::Execution ex(f);ex.fuel=1000000000;
    for(const auto make:{geometry_fixture::adjacent,geometry_fixture::holes,geometry_fixture::fractional}){
        {const auto source=make(ex);const auto bytes=ex.resources.bytes,fuel=ex.fuel;
        CHECK(detail::gapRegions(ex,source,0).get()==source.get());
        CHECK(ex.resources.bytes==bytes);CHECK(ex.fuel==fuel);
        for(const f32 width:{0.25f,0.5f,1.f,2.f,20.f}){
            const auto start=ex.fuel;const auto gap=detail::gapRegions(ex,source,width);const auto spent=start-ex.fuel;
            const auto next=ex.fuel;const auto offset=detail::offsetRegions(ex,source,width*0.5f);
            CHECK(next-ex.fuel==spent);sameGeometry(*gap,*offset);
            const auto twice=detail::gapRegions(ex,gap,width);
            const auto twiceOffset=detail::offsetRegions(ex,offset,width*0.5f);sameGeometry(*twice,*twiceOffset);
            if(twice.get()!=source.get())CHECK(twice->source.get()==source.get());
        }
        for(const f32 width:{-1.f,std::numeric_limits<f32>::infinity(),std::numeric_limits<f32>::quiet_NaN()}){
            CHECK_THROWS(detail::gapRegions(ex,source,width));CHECK(ex.resources.bytes==bytes);
        }}CHECK(ex.resources.bytes==0);
    }
    CHECK_THROWS(detail::gapRegions(ex,{},0));
    {detail::GeometryBuilder b(ex,GeometryOrigin::Contours);const auto empty=std::move(b).finish();
        CHECK(detail::gapRegions(ex,empty,2).get()==empty.get());CHECK_THROWS(detail::gapRegions(ex,empty,-1));}
    CHECK(ex.resources.bytes==0);
}
TEST_CASE("gap frontend binds named arguments roundtrips and keeps dead-result validation","[gap-bevel-v09]") {
    const std::string text=R"(asset t{size 8x4 output list point trace
        pass(rs:regions2d)->regions2d{return rs}
        draw{rs=regions(voronoi({0,0},{8,4},[{2,2},{6,2}]))
            next=pass(regions.gap(width=1,source=rs))
            trace={regions.area(next,0),regions.area(next,1)}
            trace={regions.area(rs,0),regions.area(regions.gap(next,1),0)}
        }})";
    for(const bool optimize:{false,true}){
        const auto bytes=compile(text,optimize);const auto f=loadFile(bytes);validate(f);VM vm(f,{});vm.run();
        const auto result=vm.result();REQUIRE(result.outputs[0].points.size()==2);
        CHECK(result.outputs[0].points[0].x==9);CHECK(result.outputs[0].points[0].y==9);
        CHECK(result.outputs[0].points[1].x==16);CHECK(result.outputs[0].points[1].y==4);
        const auto assembly=disassemble(bytes);CHECK(disassemble(assemble(assembly))==assembly);
        CHECK_NOTHROW(compile(ips::formatProgram(ips::parse(text)),optimize));
        for(const auto bad:{"rs=regions(voronoi({0,0},{8,4},1,0)) regions.gap(rs,-1)",
            "if(seed==0){rs=regions(voronoi({0,0},{8,4},1,0))} regions.gap(rs,0)"}){
            const auto invalid=loadFile(compile("asset t{size 8x4 draw{"+std::string(bad)+"}}",optimize));
            GenerationInputs in;in.seed=1;VM broken(invalid,in);CHECK_THROWS(broken.run());
        }
    }
    CHECK_THROWS(compile("asset t{size 1x1 draw{regions.gap(1,2)}}"));
    CHECK_THROWS(compile("asset t{size 1x1 const rs=regions.gap(regions(voronoi({0,0},{1,1},1,0)),0) draw{}}"));
}
