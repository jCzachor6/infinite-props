// Internal complete-topology probe, built for native and WASM. Arguments encode
// f32 as u32 bits, so parsing never depends on decimal conversion libraries.
#include "iprop/Voronoi.hpp"
#include "iprop/VmInternals.hpp"
#include <bit>
#include <cstdio>
#include <cstdlib>

using namespace iprop;
namespace {
u32 integer(const char* text) { return static_cast<u32>(std::strtoull(text,nullptr,10)); }
f32 number(const char* text) { return std::bit_cast<f32>(integer(text)); }
void points(const auto& values) {
    std::printf("[");
    for(std::size_t i=0;i<values.size();++i){if(i)std::printf(",");std::printf("[%u,%u]",std::bit_cast<u32>(values[i].x),std::bit_cast<u32>(values[i].y));}
    std::printf("]");
}
}
int main(int argc,char** argv) {
    if(argc<7)return 1;
    LoadedFile file;detail::Execution ex(file);ex.fuel=1000000000;
    const Vec2 origin{number(argv[2]),number(argv[3])},size{number(argv[4]),number(argv[5])};
    try {
        {
            std::shared_ptr<const GeometryData> d;
            if(argv[1][0]=='g') {
                if(argc!=18)return 2;
                detail::VoronoiOptions o;o.count=static_cast<i32>(integer(argv[6]));o.seed=std::bit_cast<i32>(integer(argv[7]));
                o.method=static_cast<i32>(integer(argv[8]));o.jitter=number(argv[9]);o.relax=static_cast<i32>(integer(argv[10]));
                o.anisotropy=number(argv[11]);o.direction={number(argv[12]),number(argv[13])};
                o.rowBias=number(argv[14]);o.rowJitter=number(argv[15]);o.rowOffset=number(argv[16]);
                // argv[17] selects a fuel ceiling without changing the fixture.
                ex.fuel=std::strtoull(argv[17],nullptr,10);
                d=detail::generatedVoronoi(ex,origin,size,o);
            } else {
                if((argc-7)%2)return 3;
                std::array<Vec2,kMaxGeometrySites> sites{};
                const auto count=static_cast<u32>((argc-7)/2);if(count>sites.size())return 4;
                for(u32 i=0;i<count;++i)sites[i]={number(argv[7+2*i]),number(argv[8+2*i])};
                d=detail::explicitVoronoi(ex,origin,size,std::span(sites.data(),count),static_cast<i32>(integer(argv[6])));
            }
            std::printf("{\"sites\":");points(d->sites);std::printf(",\"points\":");points(d->points);
            std::printf(",\"edges\":[");
            for(u32 i=0;i<d->edges.size();++i){if(i)std::printf(",");const auto& e=d->edges[i];const auto p=e.provenance;
                std::printf("[%u,%u,%u,%d,%d,%u]",e.first,e.second,p.edge,p.source,p.neighbor,static_cast<u32>(p.origin));}
            std::printf("],\"rings\":[");
            for(u32 i=0;i<d->rings.size();++i){if(i)std::printf(",");const auto r=d->rings[i];std::printf("[");
                for(u32 j=0;j<r.count;++j){if(j)std::printf(",");const auto use=d->uses[r.begin+j];const auto e=d->edges[use.edge];std::printf("%u",use.reversed?e.second:e.first);}std::printf("]");}
            std::printf("],\"regions\":[");
            for(u32 i=0;i<d->regions.size();++i){if(i)std::printf(",");const auto r=d->regions[i];
                std::printf("[%d,%u,%u,%u,%u,%u,%u,%u,[",r.sourceId,std::bit_cast<u32>(r.area),std::bit_cast<u32>(r.center.x),std::bit_cast<u32>(r.center.y),
                    std::bit_cast<u32>(r.boundsOrigin.x),std::bit_cast<u32>(r.boundsOrigin.y),std::bit_cast<u32>(r.boundsSize.x),std::bit_cast<u32>(r.boundsSize.y));
                bool comma=false;for(const auto n:d->regionNeighbors(i)){if(comma)std::printf(",");std::printf("%d",n);comma=true;}std::printf("]]");}
            std::printf("],\"bytes\":%llu,\"fuel\":%llu",static_cast<unsigned long long>(ex.resources.bytes),static_cast<unsigned long long>(1000000000-ex.fuel));
        }
        std::printf(",\"released\":%llu}\n",static_cast<unsigned long long>(ex.resources.bytes));
        return ex.resources.bytes ? 5 : 0;
    } catch(const std::exception& error) {std::fprintf(stderr,"%s\n",error.what());return 6;}
}
