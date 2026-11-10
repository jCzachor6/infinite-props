#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include "iprop/Voronoi.hpp"
#include "iprop/VmInternals.hpp"
#include "iprop/VM.hpp"
#include "Compiler.hpp"
#include "ipa.hpp"
#include "ipdis.hpp"
#include "ips/Parser.hpp"
#include "ips/Printer.hpp"
#include "ips/Semantics.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

using namespace iprop;
namespace {
std::vector<u8> compile(const std::string& text,bool optimize=true) {
    const auto p=ips::parse(text);ips::checkAsset(p,{});return compileProgram(p,{},optimize);
}
void partition(const GeometryData& d,Vec2 origin,Vec2 size,const detail::VoronoiOptions& o={}) {
    const auto count=d.sites.size();
    REQUIRE(d.regions.size()==count);REQUIRE(d.rings.size()==count);
    std::vector<std::vector<u32>> owners(d.edges.size());
    double area=0;
    const double length=std::sqrt(static_cast<double>(o.direction.x)*o.direction.x+static_cast<double>(o.direction.y)*o.direction.y);
    const double ux=o.direction.x/length,uy=o.direction.y/length,a=o.anisotropy;
    const auto distance=[&](Vec2 p,Vec2 site) {
        const double x=static_cast<double>(p.x)-site.x,y=static_cast<double>(p.y)-site.y;
        return std::pow((x*ux+y*uy)/a,2)+std::pow(-x*uy+y*ux,2);
    };
    for (u32 i=0;i<count;++i) {
        const auto& r=d.regions[i];const auto& ring=d.rings[r.outer];area+=r.area;
        CHECK(r.sourceId==static_cast<i32>(i));CHECK(r.area>0);
        REQUIRE(ring.count>=3);CHECK_FALSE(ring.hole);
        std::vector<i32> neighbors;
        for (u32 k=0;k<ring.count;++k) {
            const auto use=d.uses[ring.begin+k];const auto p=d.start(use);owners[use.edge].push_back(i);
            CHECK(p.x>=origin.x);CHECK(p.x<=origin.x+size.x);CHECK(p.y>=origin.y);CHECK(p.y<=origin.y+size.y);
            const double own=distance(p,d.sites[i]);
            for (u32 j=0;j<count;++j) CHECK(own<=distance(p,d.sites[j])+2e-5*(static_cast<double>(size.x)*size.x+static_cast<double>(size.y)*size.y));
        }
        const auto actual=d.regionNeighbors(i);
        CHECK(std::is_sorted(actual.begin(),actual.end()));
        CHECK(std::adjacent_find(actual.begin(),actual.end())==actual.end());
        for (const auto j:actual) {
            REQUIRE(j>=0);REQUIRE(static_cast<std::size_t>(j)<count);CHECK(j!=static_cast<i32>(i));
            const auto back=d.regionNeighbors(static_cast<u32>(j));
            CHECK(std::find(back.begin(),back.end(),static_cast<i32>(i))!=back.end());
        }
    }
    CHECK(std::abs(area-static_cast<double>(size.x)*size.y)<=2e-6*static_cast<double>(size.x)*size.y);
    for (u32 i=0;i<owners.size();++i) {
        const auto& e=d.edges[i];REQUIRE(e.first<e.second);REQUIRE(e.second<d.points.size());
        REQUIRE((owners[i].size()==1 || owners[i].size()==2));
        if (owners[i].size()==2) {
            CHECK(e.provenance.origin==EdgeOrigin::Diagram);
            for (const auto cell:owners[i]) {
                const auto neighbors=d.regionNeighbors(cell);
                const auto other=owners[i][0]==cell?owners[i][1]:owners[i][0];
                CHECK(std::find(neighbors.begin(),neighbors.end(),static_cast<i32>(other))!=neighbors.end());
            }
        } else {
            CHECK(e.provenance.origin==EdgeOrigin::Boundary);
            const auto p=d.points[e.first],q=d.points[e.second];
            CHECK(((p.x==q.x && (p.x==origin.x || p.x==origin.x+size.x)) ||
                   (p.y==q.y && (p.y==origin.y || p.y==origin.y+size.y))));
        }
    }
}
void equal(const GeometryData& a,const GeometryData& b) {
    REQUIRE(a.points.size()==b.points.size());REQUIRE(a.sites.size()==b.sites.size());
    REQUIRE(a.edges.size()==b.edges.size());REQUIRE(a.uses.size()==b.uses.size());
    for(u32 i=0;i<a.points.size();++i)CHECK(a.points[i]==b.points[i]);
    for(u32 i=0;i<a.sites.size();++i)CHECK(a.sites[i]==b.sites[i]);
    for(u32 i=0;i<a.edges.size();++i){CHECK(a.edges[i].first==b.edges[i].first);CHECK(a.edges[i].second==b.edges[i].second);}
    for(u32 i=0;i<a.uses.size();++i){CHECK(a.uses[i].edge==b.uses[i].edge);CHECK(a.uses[i].reversed==b.uses[i].reversed);}
    for(u32 i=0;i<a.regions.size();++i){CHECK(a.regions[i].area==b.regions[i].area);CHECK(a.regions[i].center==b.regions[i].center);}
}
}

TEST_CASE("Voronoi explicit sites cover the rectangle with one shared edge per boundary","[voronoi-v09]") {
    LoadedFile file;detail::Execution ex(file);ex.fuel=100000000;
    for (const auto& sites:std::vector<std::vector<Vec2>>{
        {{1,1}},{{0,0},{8,0}},{{0,0},{8,0},{8,6},{0,6}},
        {{1,3},{3,3},{5,3},{7,3}},{{4,0},{4,2},{4,4},{4,6}},
        {{2,2},{6,2},{6,4},{2,4}},{{4,3},{0,0},{8,0},{8,6},{0,6}},
        {{0,0},{0.000001f,0},{8,6}}}) {
        const auto data=detail::explicitVoronoi(ex,{0,0},{8,6},sites,0);
        partition(*data,{0,0},{8,6});
        for(u32 i=0;i<sites.size();++i)CHECK(data->sites[i]==sites[i]);
    }
    const std::array<Vec2,4> square{{{2,2},{6,2},{6,6},{2,6}}};
    {
        const auto d=detail::explicitVoronoi(ex,{0,0},{8,8},square,0);
        CHECK(d->points.size()==9);CHECK(d->edges.size()==12);
        for(u32 i=0;i<4;++i){CHECK(d->regions[i].area==16);CHECK(d->regionNeighbors(i).size()==2);}
        CHECK(std::find(d->regionNeighbors(0).begin(),d->regionNeighbors(0).end(),2)==d->regionNeighbors(0).end());
    }
    CHECK(ex.resources.bytes==0);
}

TEST_CASE("Voronoi generators metric rows and Lloyd preserve a bounded partition","[voronoi-v09]") {
    LoadedFile file;detail::Execution ex(file);ex.fuel=1000000000;
    for (const i32 count:{1,2,5,17,256}) for (const i32 method:{0,1}) for (const f32 jitter:{0.f,1.f}) {
        detail::VoronoiOptions o;o.count=count;o.method=method;o.jitter=jitter;o.seed=-19;
        {const auto d=detail::generatedVoronoi(ex,{-7,-3},{32,24},o);partition(*d,{-7,-3},{32,24},o);}
        CHECK(ex.resources.bytes==0);
    }
    for (const f32 a:{0.25f,1.f,4.f}) for (const Vec2 direction:{Vec2{1,0},Vec2{0,-1},Vec2{3,4},Vec2{-3,-4}}) {
        detail::VoronoiOptions o;o.count=19;o.anisotropy=a;o.direction=direction;o.seed=71;
        o.rowBias=1;o.rowJitter=1;o.rowOffset=1;o.relax=4;
        const auto d=detail::generatedVoronoi(ex,{-4,-6},{31,17},o);partition(*d,{-4,-6},{31,17},o);
    }
    CHECK(ex.resources.bytes==0);
    detail::VoronoiOptions max;max.count=256;max.relax=4;
    {const auto d=detail::generatedVoronoi(ex,{0,0},{256,256},max);partition(*d,{0,0},{256,256},max);}
    CHECK(ex.resources.bytes==0);
}

TEST_CASE("Voronoi neutral parameters are bit identical and grid partial rows are centered","[voronoi-v09]") {
    LoadedFile file;detail::Execution ex(file);ex.fuel=100000000;
    detail::VoronoiOptions o;o.count=7;o.seed=18;
    const auto base=detail::generatedVoronoi(ex,{0,0},{12,8},o);
    o.direction={-3,4};o.rowJitter=1;o.rowOffset=1;
    const auto neutral=detail::generatedVoronoi(ex,{0,0},{12,8},o);equal(*base,*neutral);
    o.method=1;o.jitter=0;o.rowBias=0;o.relax=0;
    const auto grid=detail::generatedVoronoi(ex,{0,0},{12,8},o);
    const std::array<Vec2,7> expected{{{1.5f,2},{4.5f,2},{7.5f,2},{10.5f,2},{2,6},{6,6},{10,6}}};
    for(u32 i=0;i<expected.size();++i)CHECK(grid->sites[i]==expected[i]);
    o.seed=911;const auto otherSeed=detail::generatedVoronoi(ex,{0,0},{12,8},o);equal(*grid,*otherSeed);
    // One Lloyd step is exactly the f32 centroid of the preceding clipped cells.
    o.seed=18;o.relax=1;const auto lloyd=detail::generatedVoronoi(ex,{0,0},{12,8},o);
    for(u32 i=0;i<grid->sites.size();++i)CHECK(lloyd->sites[i]==grid->regions[i].center);
}

TEST_CASE("Voronoi handles translated and nearly degenerate representable geometry","[voronoi-v09]") {
    LoadedFile file;detail::Execution ex(file);ex.fuel=1000000000;
    for(const f32 delta:{-0.0000005f,0.f,0.0000005f}) {
        const std::array<Vec2,4> sites{{{2,2},{6,2},{6,6+delta},{2,6}}};
        const auto d=detail::explicitVoronoi(ex,{0,0},{8,8},sites,0);partition(*d,{0,0},{8,8});
    }
    {
        const Vec2 origin{1e10f,-1e10f},size{8192,4096};
        detail::VoronoiOptions o;o.count=8;o.method=1;o.jitter=0;o.relax=4;
        const auto d=detail::generatedVoronoi(ex,origin,size,o);partition(*d,origin,size,o);
    }
    for(i32 seed=0;seed<100;++seed) {
        detail::VoronoiOptions o;o.count=2+seed%39;o.seed=seed;o.direction={-3,7};
        o.anisotropy=seed%2?0.25f:4.f;o.rowBias=0.7f;o.rowJitter=1;o.rowOffset=0.5f;
        const auto d=detail::generatedVoronoi(ex,{-1,-3},{17,5},o);partition(*d,{-1,-3},{17,5},o);
    }
    {
        detail::VoronoiOptions o;o.count=5;
        const auto ulp=std::nextafter(1.f,2.f)-1.f;
        CHECK_THROWS_AS(detail::generatedVoronoi(ex,{1,1},{ulp,ulp},o),Trap);
    }
    CHECK(ex.resources.bytes==0);
}

TEST_CASE("Voronoi rejects invalid domains and refunds scratch on quota and fuel traps","[voronoi-v09]") {
    LoadedFile file;detail::Execution ex(file);ex.fuel=100000000;
    const auto reject=[&](detail::VoronoiOptions o) {
        CHECK_THROWS_AS(detail::generatedVoronoi(ex,{0,0},{8,8},o),Trap);CHECK(ex.resources.bytes==0);
    };
    detail::VoronoiOptions o;
    for(const i32 count:{-1,0,257}){o.count=count;reject(o);}o={};
    for(const i32 method:{-1,2}){o.method=method;reject(o);}o={};
    for(const i32 relax:{-1,5}){o.relax=relax;reject(o);}o={};
    const auto nan=std::numeric_limits<f32>::quiet_NaN(),inf=std::numeric_limits<f32>::infinity();
    for(const f32 value:{-0.1f,1.1f,nan,inf}) {
        o={};o.jitter=value;reject(o);o={};o.rowBias=value;reject(o);
        o={};o.rowJitter=value;reject(o);o={};o.rowOffset=value;reject(o);
    }
    for(const f32 value:{0.f,0.24f,4.1f,nan,inf}){o={};o.anisotropy=value;reject(o);}
    for(const Vec2 direction:{Vec2{0,0},Vec2{nan,0},Vec2{0,inf}}){o={};o.direction=direction;reject(o);}
    for(const auto& sites:std::vector<std::vector<Vec2>>{{},{{1,1},{1,1}},{{-1,0}},{{9,2}},{{nan,1}},{{0,inf}}}) {
        CHECK_THROWS_AS(detail::explicitVoronoi(ex,{0,0},{8,8},sites,0),Trap);CHECK(ex.resources.bytes==0);
    }
    o={};
    for(const Vec2 size:{Vec2{0,1},Vec2{-1,1},Vec2{1,inf}}) {
        CHECK_THROWS_AS(detail::generatedVoronoi(ex,{0,0},size,o),Trap);CHECK(ex.resources.bytes==0);
    }
    ex.resources.bytes=kMaxGenerationData-100;
    CHECK_THROWS_AS(detail::generatedVoronoi(ex,{0,0},{8,8},o),Trap);CHECK(ex.resources.bytes==kMaxGenerationData-100);
    ex.resources.bytes=0;ex.fuel=0;
    CHECK_THROWS_AS(detail::generatedVoronoi(ex,{0,0},{8,8},o),Trap);CHECK(ex.resources.bytes==0);
}

TEST_CASE("Voronoi fuel and shared arena boundaries include every Lloyd iteration","[voronoi-v09]") {
    LoadedFile file;
    detail::VoronoiOptions o;o.count=7;o.seed=18;o.relax=4;o.rowBias=1;o.direction={3,4};
    u64 cost=0;
    {
        detail::Execution ex(file);ex.fuel=1000000;
        {const auto d=detail::generatedVoronoi(ex,{0,0},{12,8},o);CHECK(d->sites.size()==7);cost=1000000-ex.fuel;}
        CHECK(ex.resources.bytes==0);
    }
    for(const bool enough:{false,true}) {
        detail::Execution ex(file);ex.fuel=cost-(enough?0:1);bool success=false;
        try {const auto d=detail::generatedVoronoi(ex,{0,0},{12,8},o);success=true;}
        catch(const Trap& error){CHECK(error.reason==ErrorReason::FuelExhausted);}
        CHECK(success==enough);CHECK(ex.resources.bytes==0);
    }
    u64 low=0,high=1<<20;
    while(low<high) {
        const auto available=(low+high)/2;
        detail::Execution ex(file);ex.fuel=1000000;ex.resources.bytes=kMaxGenerationData-available;bool success=false;
        try {const auto d=detail::generatedVoronoi(ex,{0,0},{12,8},o);success=true;}
        catch(const Trap& error){CHECK(error.reason==ErrorReason::OutOfMemory);}
        CHECK(ex.resources.bytes==kMaxGenerationData-available);
        if(success)high=available;else low=available+1;
    }
    CHECK(low>0);CHECK(low<1<<20);
}

TEST_CASE("Voronoi language overloads queries helpers and format round trips execute","[voronoi-v09]") {
    // The constructor is a builtin like any other: a helper cannot take its name.
    CHECK_THROWS_WITH(compile("asset t{size 1x1 voronoi(v:i32)->i32{return v+1}draw{}}"),
        "function 'voronoi' has the name of a builtin; choose another name");
    for(const std::string call:{
        "voronoi({0,0},{8,8},4,seed)","voronoi({0,0},{8,8},4,seed,voronoi.grid,0,0)",
        "voronoi({0,0},{8,8},4,seed,voronoi.random,1,1,2,{3,4})",
        "voronoi({0,0},{8,8},4,seed,voronoi.grid,1,4,0.25,{1,-1},1,1,0.5)",
        "voronoi({0,0},{8,8},[{0,0},{8,0},{8,8},{0,8}])",
        "voronoi(relax=4,sites=[{0,0},{8,0},{8,8},{0,8}],size={8,8},origin={0,0})"}) {
        const auto source="asset t{size 8x8 output list point sites echo(d:diagram2d)->diagram2d{return d}draw{d=echo("+call+") "
            "for i in 0..diagram.count(d){sites=diagram.site(d,i) n=diagram.neighbors(d,i) for j in 0..list.len(n){pixel(i,j,#123)}}}}";
        for(const bool optimize:{false,true}) {
            const auto bytes=compile(source,optimize);const auto file=loadFile(bytes);validate(file);VM vm(file,{});CHECK_NOTHROW(vm.run());
            CHECK_NOTHROW(validate(loadFile(assemble(disassemble(bytes)))));
            const auto formatted=ips::formatProgram(ips::parse(source));CHECK(ips::formatProgram(ips::parse(formatted))==formatted);
            CHECK_NOTHROW(compile(formatted,optimize));
        }
    }
    for(const auto query:{"diagram.site(d,-1)","diagram.neighbors(d,1)"}) {
        const auto file=loadFile(compile("asset t{size 1x1 draw{d=voronoi({0,0},{1,1},1,0) "+std::string(query)+"}}"));
        VM vm(file,{});CHECK_THROWS_AS(vm.run(),Trap);
    }
}
