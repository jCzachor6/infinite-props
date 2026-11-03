#include "support/regions_fixture.hpp"
#include "support/contour_fixture.hpp"
#include "support/geometry_json.hpp"
#include "iprop/Voronoi.hpp"
#include <bit>
#include <charconv>
#include <cstdlib>
#include <cstring>

using namespace iprop;
namespace {
u32 integer(const char* text){return static_cast<u32>(std::strtoull(text,nullptr,10));}
f32 number(const char* text){return std::bit_cast<f32>(integer(text));}
struct Input {
    const char* at;
    u32 value(){u32 value=0;const auto result=std::from_chars(at,at+std::strlen(at),value);if(result.ec!=std::errc{})throw std::runtime_error("invalid probe input");at=result.ptr;if(*at==',')++at;return value;}
    f32 scalar(){return std::bit_cast<f32>(value());}
    Vec2 point(){const auto x=scalar(),y=scalar();return{x,y};}
};
}
int main(int argc,char** argv){
    if(argc!=11&&argc!=17)return 1;LoadedFile file;file.code.functions.emplace_back();detail::Execution ex(file);ex.fuel=1000000000;
    const u64 budget=std::strtoull(argv[9],nullptr,10),occupied=std::strtoull(argv[10],nullptr,10);
    try{
        ex.target=RenderTarget::create(1,1,{number(argv[7]),number(argv[8])});
        {
            std::shared_ptr<const GeometryData> source;Input input{argv[2]};const auto kind=integer(argv[1]);
            if(kind==0){
                const auto origin=input.point(),size=input.point();const auto n=input.value();std::vector<Vec2> sites;
                for(u32 i=0;i<n;++i)sites.push_back(input.point());source=detail::explicitVoronoi(ex,origin,size,sites,0);
            }else if(kind==1){
                const auto n=input.value();std::vector<test_support::RegionRingInput> rings;std::vector<std::vector<Vec2>> points;points.resize(n);rings.reserve(n);
                for(u32 i=0;i<n;++i){const auto parent=input.value(),count=input.value();const auto id=std::bit_cast<i32>(input.value());
                    for(u32 k=0;k<count;++k)points[i].push_back(input.point());rings.push_back({points[i],parent,id});}
                source=test_support::regionFixture(ex,rings);
            }else if(kind==2){
                const auto w=input.value(),h=input.value();std::vector<u8> pixels;for(u32 i=0;i<w*h;++i)pixels.push_back(static_cast<u8>(input.value()));
                const auto mask=test_support::contourMask(ex,w,h,pixels);source=detail::maskContours(ex,mask,{});
            }else return 2;
            if(*input.at)return 2;
            const auto sourceBytes=ex.resources.bytes,sourceFuel=1000000000-ex.fuel;ex.fuel=budget;ex.resources.bytes+=occupied;
            auto result=source;
            for(u32 i=0;i<integer(argv[6]);++i)result=detail::gapRegions(ex,result,number(argv[3]),number(argv[4]),std::bit_cast<i32>(integer(argv[5])));
            const auto geometryFuel=budget-ex.fuel,retained=ex.resources.bytes-occupied;
            const auto mask=detail::rasterRegions(ex,*result);const auto& b=*mask.buffer;
            LayerValue layer;
            if(argc==17){const auto color=[](u32 n){return Color{static_cast<u8>(n),static_cast<u8>(n>>8),static_cast<u8>(n>>16),static_cast<u8>(n>>24)};};
                layer=detail::bevelRegions(ex,*result,number(argv[11]),{number(argv[12]),number(argv[13])},color(integer(argv[14])),color(integer(argv[15])),number(argv[16]));}
            std::printf("{\"source\":");test_support::geometryJson(*source);std::printf(",\"result\":");test_support::geometryJson(*result);
            std::printf(",\"mask\":{\"origin\":[%d,%d],\"size\":[%u,%u],\"scale\":",mask.mapping.origin.x,mask.mapping.origin.y,b.raster.width,b.raster.height);
            test_support::jsonPoint({b.raster.scaleX,b.raster.scaleY});std::printf(",\"pixels\":[");
            for(u32 i=0;i<b.coverage.size();++i){if(i)std::printf(",");std::printf("%u",b.coverage[i]);}std::printf("]}");
            std::printf(",\"shared\":%s,\"sourceBytes\":%llu,\"sourceFuel\":%llu,\"bytes\":%llu,\"fuel\":%llu,\"geometryFuel\":%llu",
                result.get()==source.get()?"true":"false",static_cast<unsigned long long>(sourceBytes),static_cast<unsigned long long>(sourceFuel),
                static_cast<unsigned long long>(retained),static_cast<unsigned long long>(budget-ex.fuel),static_cast<unsigned long long>(geometryFuel));
            if(layer.buffer){const auto& raster=layer.buffer->raster;
                std::printf(",\"layer\":{\"origin\":[%d,%d],\"size\":[%u,%u],\"scale\":",layer.mapping.origin.x,layer.mapping.origin.y,raster.width,raster.height);
                test_support::jsonPoint({raster.scaleX,raster.scaleY});std::printf(",\"pixels\":[");
                bool comma=false;for(const auto p:raster.pixels)for(const auto channel:{p.r,p.g,p.b,p.a}){if(comma)std::printf(",");comma=true;std::printf("%u",channel);}std::printf("]}");}
        }
        ex.resources.bytes-=occupied;std::printf(",\"released\":%llu}\n",static_cast<unsigned long long>(ex.resources.bytes));
    }catch(const FormatError& e){std::fprintf(stderr,"%d:%s\n",static_cast<int>(e.reason),e.what());return 4;}
}
