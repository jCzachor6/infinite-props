#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include "support/contour_fixture.hpp"
#include "Compiler.hpp"
#include "ipa.hpp"
#include "ipdis.hpp"
#include "ips/Parser.hpp"
#include "ips/Printer.hpp"
#include "ips/Semantics.hpp"
#include "iprop/VM.hpp"
#include <cmath>
#include <limits>

using namespace iprop;
namespace {
std::vector<u8> compile(const std::string& text,bool optimize=true) {
    const auto p=ips::parse(text);ips::checkAsset(p,{});return compileProgram(p,{},optimize);
}
void valid(const GeometryData& d) {
    CHECK(d.origin==GeometryOrigin::Contours);CHECK(d.sites.empty());
    for (u32 i=0;i<d.regions.size();++i) {
        const auto& r=d.regions[i];CHECK(r.sourceId==static_cast<i32>(i));CHECK(r.area>0);CHECK(d.regionNeighbors(i).empty());
        CHECK_FALSE(d.rings[r.outer].hole);
        for (const auto h:d.regionHoles(i)) {CHECK(d.rings[h].hole);CHECK(d.rings[h].parent==r.outer);}
    }
    for (u32 i=0;i<d.rings.size();++i) {
        const auto& r=d.rings[i];CHECK(r.count>=3);CHECK(r.count<=2048);CHECK((r.area<0)==r.hole);
        if (r.parent!=kNoGeometryIndex) CHECK(r.parent<i);
        const auto first=d.start(d.uses[r.begin]);
        for (u32 k=0;k<r.count;++k) {
            const auto a=d.start(d.uses[r.begin+k]);CHECK(std::pair(first.y,first.x)<=std::pair(a.y,a.x));
            CHECK(d.end(d.uses[r.begin+k])==d.start(d.uses[r.begin+(k+1)%r.count]));
        }
    }
}
}

TEST_CASE("contours close full and empty rasters without a halo at every threshold","[contours-v09]") {
    LoadedFile f;detail::Execution ex(f);ex.fuel=100000000;
    for (const auto threshold:{0.f,0.5f,1.f}) for (const auto a:{u8{0},u8{255}}) {
        {const std::array<u8,6> pixels{a,a,a,a,a,a};const auto mask=test_support::contourMask(ex,3,2,pixels);
        const auto d=detail::maskContours(ex,mask,{threshold});valid(*d);
        REQUIRE(d->regions.size()==(a?1:0));
        if (a) { CHECK(d->regions[0].area==6);CHECK(d->regions[0].center==Vec2{1.5f,1});CHECK(d->rings[0].count==4); }}
        CHECK(ex.resources.bytes==0);
    }
    {const std::array<u8,1> p{1};const auto m=test_support::contourMask(ex,1,1,p);
    CHECK(detail::maskContours(ex,m,{0})->regions.size()==1);CHECK(detail::maskContours(ex,m,{1})->regions.empty());}
    CHECK(ex.resources.bytes==0);
}

TEST_CASE("contours retain holes islands saddles and canonical ordering","[contours-v09]") {
    LoadedFile f;detail::Execution ex(f);ex.fuel=100000000;
    const std::array<u8,9> ring{255,255,255,255,0,255,255,255,255};
    {const auto m=test_support::contourMask(ex,3,3,ring);const auto d=detail::maskContours(ex,m,{});valid(*d);
    REQUIRE(d->regions.size()==1);REQUIRE(d->rings.size()==2);CHECK(d->regionHoles(0).size()==1);CHECK(d->regions[0].area==8.5f);
    CHECK(d->rings[0].area==9);CHECK(d->rings[1].area==-0.5);}
    const std::array<u8,4> diagonal{255,0,0,255};
    for (i32 connectivity:{0,1}) {
        const auto m=test_support::contourMask(ex,2,2,diagonal);const auto d=detail::maskContours(ex,m,{0.5f,0,0,connectivity});valid(*d);
        CHECK(d->regions.size()==(connectivity?1:2));CHECK(d->holes.empty());
    }
    std::vector<u8> nested(81,255);
    for (u32 y=1;y<8;++y)for(u32 x=1;x<8;++x)nested[y*9+x]=0;
    for (u32 y=3;y<6;++y)for(u32 x=3;x<6;++x)nested[y*9+x]=255;
    {const auto m=test_support::contourMask(ex,9,9,nested);const auto d=detail::maskContours(ex,m,{});valid(*d);
    REQUIRE(d->regions.size()==2);REQUIRE(d->rings.size()==3);CHECK(d->holes.size()==1);
    CHECK(d->regions[0].area>d->regions[1].area);CHECK(d->rings[2].parent==1);}
    CHECK(ex.resources.bytes==0);
}

TEST_CASE("contours map source placement nonuniform scale and all D4 symmetries","[contours-v09]") {
    LoadedFile f;detail::Execution ex(f);ex.fuel=100000000;
    const std::array<u8,6> p{255,255,255,255,255,255};
    for (u8 kind=0;kind<8;++kind) {
        const auto m=test_support::contourMask(ex,3,2,p,{2,4},kind,{-7,11});
        const auto d=detail::maskContours(ex,m,{});valid(*d);REQUIRE(d->regions.size()==1);
        const auto transform=*D4Transform::fromKind(kind);
        const auto scale=transform.mapExtent(Vec2{2,4}), size=transform.mapExtent(Vec2{3,2});
        CHECK(d->regions[0].boundsOrigin==Vec2{-7/scale.x,11/scale.y});
        CHECK(d->regions[0].boundsSize==Vec2{size.x/scale.x,size.y/scale.y});
        CHECK(d->regions[0].area==0.75f);
    }
    CHECK(ex.resources.bytes==0);
}

TEST_CASE("contour simplify preserves holes and min_area filters net components","[contours-v09]") {
    LoadedFile f;detail::Execution ex(f);ex.fuel=1000000000;
    const std::array<u8,9> p{255,255,255,255,0,255,255,255,255};
    const auto m=test_support::contourMask(ex,3,3,p);
    const auto a=detail::maskContours(ex,m,{}), b=detail::maskContours(ex,m,{0.5f,1000,0,0});valid(*b);
    CHECK(b->regions.size()==1);CHECK(b->holes.size()==1);CHECK(b->points.size()<=a->points.size());
    CHECK(detail::maskContours(ex,m,{0.5f,0,8.5f,0})->regions.size()==1);
    CHECK(detail::maskContours(ex,m,{0.5f,0,8.5001f,0})->regions.empty());
}

TEST_CASE("contour source frontend handles named calls helpers formatting and traps","[contours-v09]") {
    const std::string text="asset t{size 3x3 echo(c:contours2d)->contours2d{return c}draw{layer image{rect(0,0,3,3,#fff)} m=mask.from(image) a=echo(contour(m,0.5)) b=contour(connectivity=contour.eight,min_area=0,simplify=0,threshold=0.5,source=m)}}";
    for (const bool optimize:{false,true}) {
        const auto bytes=compile(text,optimize);const auto f=loadFile(bytes);validate(f);VM vm(f,{});CHECK_NOTHROW(vm.run());
        const auto assembly=disassemble(bytes);const auto round=assemble(assembly);
        CHECK(disassemble(round)==assembly);CHECK_NOTHROW(VM(loadFile(round),{}).run());
        const auto formatted=ips::formatProgram(ips::parse(text));CHECK(ips::formatProgram(ips::parse(formatted))==formatted);
        const auto f2=loadFile(compile(formatted,optimize));CHECK_NOTHROW(VM(f2,{}).run());
        const auto helper=compile("asset t{size 1x1 contour(x:i32)->i32{return x}draw{pixel(contour(0),0,#fff)}}",optimize);
        CHECK_NOTHROW(VM(loadFile(helper),{}).run());
        const auto unset=loadFile(compile("asset t{size 1x1 draw{if(false){layer l{} m=mask.from(l)} contour(m,0.5)}}",optimize));
        CHECK_THROWS_WITH(VM(unset,{}).run(),Catch::Matchers::ContainsSubstring("unassigned"));
    }
}

TEST_CASE("contours validate domains and release scratch on fuel quota and size traps","[contours-v09]") {
    LoadedFile f;detail::Execution ex(f);ex.fuel=1000000000;
    const std::array<u8,4> p{255,0,0,255};const auto m=test_support::contourMask(ex,2,2,p);const auto baseline=ex.resources.bytes;
    for (const auto options:{detail::ContourOptions{-1},detail::ContourOptions{2},detail::ContourOptions{std::numeric_limits<f32>::quiet_NaN()},
        detail::ContourOptions{0.5f,-1},detail::ContourOptions{0.5f,0,-1},detail::ContourOptions{0.5f,0,0,2}}) {
        CHECK_THROWS_AS(detail::maskContours(ex,m,options),Trap);CHECK(ex.resources.bytes==baseline);
    }
    for (const auto bad:{std::numeric_limits<f32>::infinity(),std::numeric_limits<f32>::quiet_NaN()})for(u32 field=0;field<3;++field) {
        detail::ContourOptions o;
        if(field==0)o.threshold=bad;else if(field==1)o.simplify=bad;else o.minArea=bad;
        CHECK_THROWS_AS(detail::maskContours(ex,m,o),Trap);CHECK(ex.resources.bytes==baseline);
    }
    CHECK_THROWS_AS(detail::maskContours(ex,MaskValue{},{}),Trap);CHECK(ex.resources.bytes==baseline);
    const auto before=ex.fuel;{const auto d=detail::maskContours(ex,m,{});valid(*d);}const auto cost=before-ex.fuel;
    ex.fuel=cost;CHECK_NOTHROW(detail::maskContours(ex,m,{}));CHECK(ex.fuel==0);CHECK(ex.resources.bytes==baseline);
    ex.fuel=cost-1;CHECK_THROWS_AS(detail::maskContours(ex,m,{}),Trap);CHECK(ex.resources.bytes==baseline);
    ex.fuel=1000000000;ex.resources.bytes=kMaxGenerationData-4095;
    CHECK_THROWS_AS(detail::maskContours(ex,m,{}),Trap);CHECK(ex.resources.bytes==kMaxGenerationData-4095);ex.resources.bytes=baseline;
    // min_area cannot conceal an oversized unfiltered component set.
    std::vector<u8> checker(35*35,0);for(u32 y=0;y<35;y+=2)for(u32 x=0;x<35;x+=2)checker[y*35+x]=255;
    const auto many=test_support::contourMask(ex,35,35,checker);
    CHECK_THROWS_WITH(detail::maskContours(ex,many,{0.5f,0,1e10f,0}),Catch::Matchers::ContainsSubstring("256 unfiltered"));
}

TEST_CASE("contour extracted and normalized limits apply before simplify or filtering","[contours-v09]") {
    LoadedFile f;detail::Execution ex(f);ex.fuel=1000000000;
    {
        std::vector<u8> p(1024,255);const auto m=test_support::contourMask(ex,1024,1,p);
        const auto d=detail::maskContours(ex,m,{});valid(*d);REQUIRE(d->rings.size()==1);CHECK(d->rings[0].count==4);CHECK(d->regions[0].area==1024);
    }
    CHECK(ex.resources.bytes==0);
    {
        std::vector<u8> p(32*32,0);for(u32 y=0;y<32;y+=2)for(u32 x=0;x<32;x+=2)p[y*32+x]=255;
        const auto m=test_support::contourMask(ex,32,32,p);const auto d=detail::maskContours(ex,m,{});valid(*d);CHECK(d->regions.size()==256);
    }
    CHECK(ex.resources.bytes==0);
    {
        std::vector<u8> p(2*1400,255);for(u32 x=0;x<1400;x+=2)p[1400+x]=0;
        const auto m=test_support::contourMask(ex,1400,2,p);const auto baseline=ex.resources.bytes;
        CHECK_THROWS_WITH(detail::maskContours(ex,m,{0.5f,1e10f,1e10f,0}),Catch::Matchers::ContainsSubstring("2048 vertices"));CHECK(ex.resources.bytes==baseline);
    }
    CHECK(ex.resources.bytes==0);
    {
        std::vector<u8> p(256*256,0);for(u32 y=0;y<256;++y)for(u32 x=0;x<256;++x)if((x+y)%2==0)p[y*256+x]=255;
        const auto m=test_support::contourMask(ex,256,256,p);const auto baseline=ex.resources.bytes;
        CHECK_THROWS_WITH(detail::maskContours(ex,m,{0.5f,1e10f,1e10f,0}),Catch::Matchers::ContainsSubstring("65536 extracted"));CHECK(ex.resources.bytes==baseline);
    }
    CHECK(ex.resources.bytes==0);
}

TEST_CASE("contours agree with digital topology for every 3x3 binary raster","[contours-v09]") {
    LoadedFile f;detail::Execution ex(f);ex.fuel=1000000000;
    const auto groups=[](const std::array<u8,9>& p,bool foreground,bool eight) {
        std::array<bool,9> seen{};u32 result=0;
        for (i32 first=0;first<9;++first) {
            if (seen[static_cast<u32>(first)] || (p[static_cast<u32>(first)]!=0)!=foreground) continue;
            std::array<i32,9> queue{};u32 begin=0,end=1;queue[0]=first;seen[static_cast<u32>(first)]=true;bool boundary=false;
            while (begin<end) {
                const auto at=queue[begin++],x=at%3,y=at/3;
                boundary|=x==0||x==2||y==0||y==2;
                for (i32 dy=-1;dy<=1;++dy)for(i32 dx=-1;dx<=1;++dx) {
                    if ((dx==0&&dy==0)||(!eight&&dx!=0&&dy!=0))continue;
                    const auto nx=x+dx,ny=y+dy;
                    if(nx<0||nx>=3||ny<0||ny>=3)continue;
                    const auto index=static_cast<u32>(ny*3+nx);
                    if(seen[index] || (p[index]!=0)!=foreground)continue;
                    seen[index]=true;queue[end++]=static_cast<i32>(index);
                }
            }
            if(foreground||!boundary)++result;
        }
        return result;
    };
    for (u32 bits=0;bits<512;++bits)for(i32 connectivity:{0,1})for(f32 threshold:{0.f,0.5f,1.f}) {
        INFO(bits << " connectivity " << connectivity << " threshold " << threshold);
        {
            std::array<u8,9> p{};for(u32 i=0;i<9;++i)p[i]=(bits&(1u<<i))?255:0;
            const auto m=test_support::contourMask(ex,3,3,p);
            const auto d=detail::maskContours(ex,m,{threshold,0,0,connectivity});
            valid(*d);CHECK(d->regions.size()==groups(p,true,connectivity==1));
            CHECK(d->holes.size()==groups(p,false,connectivity==0));
        }
        CHECK(ex.resources.bytes==0);
    }
}

TEST_CASE("contour capacity accounting has a sharp rollback-safe quota boundary","[contours-v09]") {
    LoadedFile f;detail::Execution ex(f);ex.fuel=1000000000;
    const std::array<u8,9> p{255,255,255,255,0,255,255,255,255};
    const auto m=test_support::contourMask(ex,3,3,p);const auto baseline=ex.resources.bytes;
    const auto fits=[&](u64 occupied) {
        ex.resources.bytes=baseline+occupied;ex.fuel=1000000000;bool ok=true;
        try {const auto d=detail::maskContours(ex,m,{0.5f,1000,0,0});valid(*d);}
        catch(const Trap& e) {CHECK(e.reason==ErrorReason::OutOfMemory);ok=false;}
        CHECK(ex.resources.bytes==baseline+occupied);ex.resources.bytes=baseline;return ok;
    };
    u64 lo=0,hi=kMaxGenerationData-baseline;
    while(lo+1<hi) {const auto mid=(lo+hi)/2;if(fits(mid))lo=mid;else hi=mid;}
    CHECK(fits(lo));CHECK_FALSE(fits(lo+1));
}
