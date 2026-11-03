#include <catch2/catch_test_macros.hpp>
#include "support/geometry_fixture.hpp"
#include "support/regions_fixture.hpp"
#include "support/contour_fixture.hpp"
#include "iprop/VM.hpp"
#include "iprop/Voronoi.hpp"
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
TEST_CASE("variable gaps share an unoriented original-edge profile and retain flat ancestry","[gap-bevel-v09]") {
    LoadedFile f;f.code.functions.emplace_back();detail::Execution ex(f);ex.fuel=1000000000;
    const std::array<Vec2,2> sites{{{4,8},{12,8}}};
    {const auto source=detail::explicitVoronoi(ex,{0,0},{16,16},sites,0);const auto bytes=ex.resources.bytes;
    for(const i32 seed:{0,7,-1,std::numeric_limits<i32>::min()}){
        const auto constant=detail::gapRegions(ex,source,1);const auto zero=detail::gapRegions(ex,source,1,0,seed);
        sameGeometry(*constant,*zero);CHECK(detail::gapRegions(ex,source,0,1,seed).get()==source.get());
        for(const f32 variation:{0.25f,1.f}){
            const auto result=detail::gapRegions(ex,source,1,variation,seed);REQUIRE(result->regions.size()==2);
            const auto repeated=detail::gapRegions(ex,source,1,variation,seed);sameGeometry(*result,*repeated);
            CHECK(result->source.get()==source.get());CHECK(result->regionNeighbors(0).size()==1);
            for(u32 j=1;j<8;++j){
                Vec2 left{},right{};bool foundLeft=false,foundRight=false;
                for(u32 i=0;i<2;++i){const auto& r=result->rings[result->regions[i].outer];
                    for(u32 k=0;k<r.count;++k){const auto p=result->start(result->uses[r.begin+k]);
                        if(p.y==static_cast<f32>(j*2)&&std::abs(p.x-8)<1){if(i==0){left=p;foundLeft=true;}else{right=p;foundRight=true;}}}}
                REQUIRE(foundLeft);REQUIRE(foundRight);CHECK(std::abs((8-left.x)-(right.x-8))<0.000002f);
            }
            const auto twice=detail::gapRegions(ex,result,0.5f,variation,seed);CHECK(twice->source.get()==source.get());
            CHECK(twice->regions[0].area<result->regions[0].area);CHECK(source->regions[0].area==128);
        }
    }
    for(const f32 variation:{-1.f,1.01f,std::numeric_limits<f32>::infinity(),std::numeric_limits<f32>::quiet_NaN()}){
        CHECK_THROWS(detail::gapRegions(ex,source,0,variation,0));CHECK(ex.resources.bytes==bytes);
    }}CHECK(ex.resources.bytes==0);
    {const auto source=geometry_fixture::holes(ex);const auto result=detail::gapRegions(ex,source,0.5f,1,7);
        CHECK(!result->holes.empty());CHECK(result->regions[0].area<source->regions[0].area);}
    CHECK(ex.resources.bytes==0);
}
TEST_CASE("variable gaps normalize every binary 3x3 contour without leaking scratch","[gap-bevel-v09]") {
    LoadedFile f;f.code.functions.emplace_back();detail::Execution ex(f);ex.fuel=1000000000000ull;
    for(u32 bits=0;bits<512;++bits){
        {std::array<u8,9> pixels{};for(u32 j=0;j<9;++j)pixels[j]=(bits&(1u<<j))?255:0;
        const auto mask=test_support::contourMask(ex,3,3,pixels);const auto source=detail::maskContours(ex,mask,{});
        for(const f32 width:{0.25f,1.f,4.f}){
            INFO(bits << " width=" << width);
            const auto result=detail::gapRegions(ex,source,width,1,17);
            CHECK(result->regions.size()<=256);
            for(const auto& ring:result->rings){CHECK(ring.count>=3);CHECK(ring.count<=2048);CHECK((ring.area<0)==ring.hole);}
            for(const auto& region:result->regions)CHECK(region.area>0);
        }}CHECK(ex.resources.bytes==0);
    }
}
TEST_CASE("bevel shades local outward normals with explicit alpha and shared raster bounds","[gap-bevel-v09]") {
    LoadedFile f;f.code.functions.emplace_back();detail::Execution ex(f);ex.fuel=1000000000;ex.target=RenderTarget::create(1,1,{1,1});
    {const std::array<Vec2,1> site{{{8,6}}};const auto source=detail::explicitVoronoi(ex,{0,0},{16,12},site,0);
    const auto bytes=ex.resources.bytes;
    {const auto layer=detail::bevelRegions(ex,*source,2,{0,-1},{255,20,40,128},{10,30,50,64},0.5f);
        CHECK(layer.buffer->format==SurfaceBuffer::Format::RGBA8);CHECK(layer.buffer->coverage.empty());
        CHECK(layer.buffer->raster.width==16);CHECK(layer.buffer->raster.height==12);
        CHECK(layer.buffer->raster.at(8,0)==Color{255,20,40,48});CHECK(layer.buffer->raster.at(8,11)==Color{10,30,50,24});
        CHECK(layer.buffer->raster.at(8,5)==Color{});CHECK(ex.canvas().at(0,0)==Color{});
        CHECK(source->regions[0].area==192);
        const auto zero=detail::bevelRegions(ex,*source,2,{0,-1},{255,255,255,255},{0,0,0,255},0);
        for(const auto pixel:zero.buffer->raster.pixels)CHECK(pixel==Color{});
        const auto reverse=detail::bevelRegions(ex,*source,2,{0,1});CHECK(reverse.buffer->raster.at(8,0).r==0);
        CHECK(reverse.buffer->raster.at(8,11).r==255);
        ex.target.canvas().scaleX=2;ex.target.canvas().scaleY=3;
        const auto scaled=detail::bevelRegions(ex,*source,2,{0,-1000},{255,20,40,128},{10,30,50,64},0.5f);
        CHECK(scaled.buffer->raster.width==32);CHECK(scaled.buffer->raster.height==36);
        CHECK(scaled.buffer->raster.at(16,0)==Color{255,20,40,59});
    }CHECK(ex.resources.bytes==bytes);ex.target.canvas().scaleX=1;ex.target.canvas().scaleY=1;
    for(const f32 width:{0.f,-1.f,std::numeric_limits<f32>::infinity(),std::numeric_limits<f32>::quiet_NaN()})CHECK_THROWS(detail::bevelRegions(ex,*source,width,{1,1}));
    for(const auto light:{Vec2{0,0},Vec2{std::numeric_limits<f32>::infinity(),1},Vec2{1,std::numeric_limits<f32>::quiet_NaN()}})CHECK_THROWS(detail::bevelRegions(ex,*source,1,light));
    for(const f32 strength:{-1.f,1.01f,std::numeric_limits<f32>::infinity(),std::numeric_limits<f32>::quiet_NaN()})CHECK_THROWS(detail::bevelRegions(ex,*source,1,{1,1},{},{},strength));
    CHECK(ex.resources.bytes==bytes);}
    CHECK(ex.resources.bytes==0);
    {detail::GeometryBuilder b(ex,GeometryOrigin::Contours);const auto empty=std::move(b).finish();
        const auto layer=detail::bevelRegions(ex,*empty,1,{1,1});CHECK(layer.buffer->raster.width==1);
        CHECK(layer.buffer->raster.pixels[0]==Color{});CHECK_THROWS(detail::bevelRegions(ex,*empty,0,{1,1}));}
    CHECK(ex.resources.bytes==0);
}
TEST_CASE("gap intermediate limits and bevel parameter failures refund before publication","[gap-bevel-v09]") {
    LoadedFile f;f.code.functions.emplace_back();detail::Execution ex(f);ex.fuel=1000000000;ex.target=RenderTarget::create(1,1,{1,1});
    {const auto source=geometry_fixture::holes(ex);const auto bytes=ex.resources.bytes;
        CHECK_THROWS(detail::gapRegions(ex,source,1e10f,1,7));CHECK(ex.resources.bytes==bytes);
        for(const f32 scale:{0.f,-1.f,std::numeric_limits<f32>::infinity(),std::numeric_limits<f32>::quiet_NaN()}){
            ex.canvas().scaleX=scale;CHECK_THROWS(detail::bevelRegions(ex,*source,1,{1,1},{},{},0));CHECK(ex.resources.bytes==bytes);
        }ex.canvas().scaleX=1;
        for(const f32 component:{std::numeric_limits<f32>::max(),std::numeric_limits<f32>::denorm_min()}){
            const auto layer=detail::bevelRegions(ex,*source,1,{component,-component});CHECK(!layer.buffer->raster.pixels.empty());
        }
    }CHECK(ex.resources.bytes==0);
}
TEST_CASE("bevel layers retain pixels independently after execution and geometry destruction","[gap-bevel-v09]") {
    LoadedFile f;LayerValue retained;
    {detail::Execution ex(f);ex.fuel=100000000;ex.target=RenderTarget::create(1,1,{2,3});
        const auto source=geometry_fixture::holes(ex);const auto gap=detail::gapRegions(ex,source,0.5f,1,7);
        retained=detail::bevelRegions(ex,*gap,1,{-1,-1});}
    CHECK(retained.buffer.use_count()==1);CHECK(retained.buffer->raster.width==20);CHECK(retained.buffer->raster.height==24);
    bool visible=false;for(const auto p:retained.buffer->raster.pixels)visible|=p.a>0;CHECK(visible);
}
TEST_CASE("gap and bevel arguments evaluate once in signature order despite reversed names","[gap-bevel-v09]") {
    const std::string text=R"(asset t{size 8x6 output list point trace
        f(tag:i32,value:f32)->f32{trace={tag,unit()}return value}
        i(tag:i32,value:i32)->i32{trace={tag,unit()}return value}
        v(tag:i32,value:vec2)->vec2{trace={tag,unit()}return value}
        c(tag:i32,value:color)->color{trace={tag,unit()}return value}
        r(tag:i32,value:regions2d)->regions2d{trace={tag,unit()}return value}
        draw{trace={0,unit()}rs=regions(voronoi({0,0},{8,6},1,7))
            next=regions.gap(seed=i(4,7),variation=f(3,0.5),width=f(2,1),source=r(1,rs))
            shade=regions.bevel(strength=f(10,0.5),shadow=c(9,#000),highlight=c(8,#fff),light=v(7,{-1,-1}),width=f(6,2),source=r(5,next))
            trace={11,unit()}
        }})";
    const auto control=loadFile(compile("asset t{size 8x6 output list point trace draw{for j in 0..12{trace={j,unit()}}}}"));
    for(const bool optimize:{false,true})for(const u32 seed:{0u,7u,0xffffffffu}){
        const auto file=loadFile(compile(text,optimize));GenerationInputs in;in.seed=seed;
        VM actual(file,in),expected(control,in);actual.run();expected.run();const auto a=actual.result(),b=expected.result();
        REQUIRE(a.outputs[0].points.size()==12);REQUIRE(b.outputs[0].points.size()==12);
        for(u32 j=0;j<12;++j){CHECK(a.outputs[0].points[j].x==b.outputs[0].points[j].x);CHECK(a.outputs[0].points[j].y==b.outputs[0].points[j].y);}
        CHECK(a.image.pixels==b.image.pixels);
    }
}
