// Internal complete-topology probe, shared by native and WASM builds.
#include "support/contour_fixture.hpp"
#include <bit>
#include <cstdio>
#include <cstdlib>
#include <cstring>

using namespace iprop;
namespace {
u32 integer(const char* s) { return static_cast<u32>(std::strtoull(s,nullptr,10)); }
f32 number(const char* s) { return std::bit_cast<f32>(integer(s)); }
void point(Vec2 p) { std::printf("[%u,%u]",std::bit_cast<u32>(p.x),std::bit_cast<u32>(p.y)); }
}
int main(int argc,char** argv) {
    if (argc!=15) return 1;
    LoadedFile file; detail::Execution ex(file);
    const u64 budget=std::strtoull(argv[13],nullptr,10), occupied=std::strtoull(argv[14],nullptr,10);
    ex.fuel=budget;
    try {
        {
            const u32 width=integer(argv[1]),height=integer(argv[2]);
            if (std::strlen(argv[3])!=2u*static_cast<u64>(width)*height) return 2;
            std::vector<u8> pixels; pixels.reserve(static_cast<std::size_t>(width)*height);
            const auto digit=[](char c){return c<='9'?c-'0':c-'a'+10;};
            for (std::size_t i=0;i<std::strlen(argv[3]);i+=2) pixels.push_back(static_cast<u8>(16*digit(argv[3][i])+digit(argv[3][i+1])));
            auto mask=test_support::contourMask(ex,width,height,pixels,{number(argv[8]),number(argv[9])},
                static_cast<u8>(integer(argv[10])),{std::bit_cast<i32>(integer(argv[11])),std::bit_cast<i32>(integer(argv[12]))});
            ex.resources.bytes+=occupied;
            const detail::ContourOptions options{number(argv[4]),number(argv[5]),number(argv[6]),static_cast<i32>(integer(argv[7]))};
            const auto d=detail::maskContours(ex,mask,options);
            std::printf("{\"points\":[");
            for (u32 i=0;i<d->points.size();++i) { if(i)std::printf(",");point(d->points[i]); }
            std::printf("],\"edges\":[");
            for (u32 i=0;i<d->edges.size();++i) { if(i)std::printf(",");const auto& e=d->edges[i];const auto p=e.provenance;
                std::printf("[%u,%u,%u,%d,%d,%u]",e.first,e.second,p.edge,p.source,p.neighbor,static_cast<u32>(p.origin)); }
            std::printf("],\"rings\":[");
            for (u32 i=0;i<d->rings.size();++i) { if(i)std::printf(",");const auto& r=d->rings[i];
                std::printf("{\"vertices\":[");
                for (u32 k=0;k<r.count;++k) { if(k)std::printf(",");const auto u=d->uses[r.begin+k];const auto e=d->edges[u.edge];std::printf("%u",u.reversed?e.second:e.first); }
                std::printf("],\"parent\":%u,\"hole\":%s,\"moments\":[\"%016llx\",\"%016llx\",\"%016llx\"]}",r.parent,r.hole?"true":"false",
                    static_cast<unsigned long long>(std::bit_cast<u64>(r.area)),static_cast<unsigned long long>(std::bit_cast<u64>(r.momentX)),static_cast<unsigned long long>(std::bit_cast<u64>(r.momentY))); }
            std::printf("],\"regions\":[");
            for (u32 i=0;i<d->regions.size();++i) { if(i)std::printf(",");const auto& r=d->regions[i];
                std::printf("{\"source\":%d,\"outer\":%u,\"area\":%u,\"center\":",r.sourceId,r.outer,std::bit_cast<u32>(r.area));point(r.center);
                std::printf(",\"origin\":");point(r.boundsOrigin);std::printf(",\"size\":");point(r.boundsSize);
                std::printf(",\"holes\":[");bool comma=false;for(const auto h:d->regionHoles(i)){if(comma)std::printf(",");std::printf("%u",h);comma=true;}
                std::printf("],\"neighbors\":[");comma=false;for(const auto n:d->regionNeighbors(i)){if(comma)std::printf(",");std::printf("%d",n);comma=true;}std::printf("]}"); }
            std::printf("],\"fuel\":%llu,\"bytes\":%llu",static_cast<unsigned long long>(budget-ex.fuel),static_cast<unsigned long long>(ex.resources.bytes-occupied));
            ex.resources.bytes-=occupied;
        }
        std::printf(",\"released\":%llu}\n",static_cast<unsigned long long>(ex.resources.bytes));
        return ex.resources.bytes?3:0;
    } catch (const std::exception& e) { std::fprintf(stderr,"%s\n",e.what()); return 4; }
}
