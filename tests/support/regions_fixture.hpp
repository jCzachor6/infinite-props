#pragma once
#include "iprop/Regions.hpp"
#include "iprop/VmInternals.hpp"

namespace iprop::test_support {
struct RegionRingInput {std::span<const Vec2> points;u32 parent=kNoGeometryIndex;i32 id=0;};
inline std::shared_ptr<const GeometryData> regionFixture(detail::Execution& ex,std::span<const RegionRingInput> rings){
    detail::GeometryBuilder b(ex,GeometryOrigin::Contours);
    detail::ResourceVector<GeometryEdgeUse,8> uses{detail::ResourceAllocator<GeometryEdgeUse,8>(ex.resources)};
    detail::ResourceVector<u32,4> holes{detail::ResourceAllocator<u32,4>(ex.resources)};
    u32 pointIndex=0,edgeIndex=0;
    for(const auto& r:rings){
        uses.clear();for(const auto p:r.points)b.point(p);
        for(u32 k=0;k<r.points.size();++k){
            const auto a=pointIndex+k,z=pointIndex+(k+1)%static_cast<u32>(r.points.size());
            const auto edge=b.edge(std::min(a,z),std::max(a,z),{edgeIndex++,r.id,-1,EdgeOrigin::Contour});uses.push_back({edge,a>z});
        }
        b.ring(uses,r.parent);pointIndex+=static_cast<u32>(r.points.size());
    }
    for(u32 i=0;i<rings.size();++i){
        u32 depth=0,parent=rings[i].parent;while(parent!=kNoGeometryIndex){++depth;parent=rings[parent].parent;}
        if(depth%2)continue;holes.clear();
        for(u32 j=0;j<rings.size();++j)if(rings[j].parent==i)holes.push_back(j);
        b.region(i,rings[i].id,holes);
    }
    return std::move(b).finish();
}
}
