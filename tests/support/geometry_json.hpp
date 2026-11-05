#pragma once
#include "iprop/Geometry.hpp"
#include <bit>
#include <cstdio>

namespace iprop::test_support {
inline void jsonPoint(Vec2 p){std::printf("[%u,%u]",std::bit_cast<u32>(p.x),std::bit_cast<u32>(p.y));}
inline void geometryJson(const GeometryData& d){
            std::printf("{\"origin\":%u,\"sites\":[",static_cast<u32>(d.origin));
            for(u32 i=0;i<d.sites.size();++i){if(i)std::printf(",");jsonPoint(d.sites[i]);}
            std::printf("],\"points\":[");
            for (u32 i=0;i<d.points.size();++i) { if(i)std::printf(",");jsonPoint(d.points[i]); }
            std::printf("],\"edges\":[");
            for (u32 i=0;i<d.edges.size();++i) { if(i)std::printf(",");const auto& e=d.edges[i];const auto p=e.provenance;
                std::printf("[%u,%u,%u,%d,%d,%u]",e.first,e.second,p.edge,p.source,p.neighbor,static_cast<u32>(p.origin)); }
            std::printf("],\"rings\":[");
            for (u32 i=0;i<d.rings.size();++i) { if(i)std::printf(",");const auto& r=d.rings[i];
                std::printf("{\"vertices\":[");
                for (u32 k=0;k<r.count;++k) { if(k)std::printf(",");const auto u=d.uses[r.begin+k];const auto e=d.edges[u.edge];std::printf("%u",u.reversed?e.second:e.first); }
                std::printf("],\"parent\":%u,\"hole\":%s,\"moments\":[\"%016llx\",\"%016llx\",\"%016llx\"]}",r.parent,r.hole?"true":"false",
                    static_cast<unsigned long long>(std::bit_cast<u64>(r.area)),static_cast<unsigned long long>(std::bit_cast<u64>(r.momentX)),static_cast<unsigned long long>(std::bit_cast<u64>(r.momentY))); }
            std::printf("],\"regions\":[");
            for (u32 i=0;i<d.regions.size();++i) { if(i)std::printf(",");const auto& r=d.regions[i];
                std::printf("{\"source\":%d,\"outer\":%u,\"area\":%u,\"center\":",r.sourceId,r.outer,std::bit_cast<u32>(r.area));jsonPoint(r.center);
                std::printf(",\"origin\":");jsonPoint(r.boundsOrigin);std::printf(",\"size\":");jsonPoint(r.boundsSize);
                std::printf(",\"holes\":[");bool comma=false;for(const auto h:d.regionHoles(i)){if(comma)std::printf(",");std::printf("%u",h);comma=true;}
                std::printf("],\"neighbors\":[");comma=false;for(const auto n:d.regionNeighbors(i)){if(comma)std::printf(",");std::printf("%d",n);comma=true;}std::printf("]}"); }
            std::printf("]}");
}
}
