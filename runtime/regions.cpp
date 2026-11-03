#include "iprop/Regions.hpp"
#include "iprop/VmInternals.hpp"

#include <cmath>

namespace iprop::detail {
namespace {
EdgeProvenance ancestry(const GeometryData& source, u32 edge) {
    auto p=source.edges[edge].provenance;
    p.edge=source.source ? p.edge : edge;
    p.origin=EdgeOrigin::Offset;
    return p;
}
}

std::shared_ptr<const GeometryData> geometryRegions(Execution& ex,
    const std::shared_ptr<const GeometryData>& source, f32 minimum) {
    if (!source) ex.trap("region source is unassigned",ErrorReason::RuntimeViolation);
    if (!std::isfinite(minimum) || minimum<0)
        ex.trap("regions min_area must be finite and nonnegative",ErrorReason::NumericRange);
    ex.resources.resourceLifetime()->location=ex.activeInstruction;
    // The default conversion is a view, including a valid empty store. The
    // other VM alternative prevents an implicit source-language conversion.
    if (minimum==0) return source;
    const ResourceReservation workspace(ex.resources,1024);
    ex.spend(source->regions.size()+source->rings.size()+source->points.size()+source->edges.size());
    ResourceVector<u32,4> regions(source->regions.size(),kNoGeometryIndex,ResourceAllocator<u32,4>(ex.resources));
    ResourceVector<u32,4> rings(source->rings.size(),kNoGeometryIndex,ResourceAllocator<u32,4>(ex.resources));
    ResourceVector<u32,4> points(source->points.size(),kNoGeometryIndex,ResourceAllocator<u32,4>(ex.resources));
    ResourceVector<u32,4> edges(source->edges.size(),kNoGeometryIndex,ResourceAllocator<u32,4>(ex.resources));
    u32 kept=0;
    for (u32 i=0;i<source->regions.size();++i) {
        ex.spend(1);
        const auto& r=source->regions[i];
        double area=source->rings[r.outer].area;
        for (const auto h:source->regionHoles(i)) { ex.spend(1);area+=source->rings[h].area; }
        if (area<minimum) continue;
        regions[i]=kept++;
        rings[r.outer]=0;
        for (const auto h:source->regionHoles(i)) { ex.spend(1);rings[h]=0; }
    }
    if (kept==source->regions.size()) return source;
    GeometryBuilder builder(ex,GeometryOrigin::Offset,source);
    ResourceVector<GeometryEdgeUse,8> uses{ResourceAllocator<GeometryEdgeUse,8>(ex.resources)};
    ResourceVector<u32,4> holes{ResourceAllocator<u32,4>(ex.resources)};
    ResourceVector<i32,4> neighbors{ResourceAllocator<i32,4>(ex.resources)};
    for (u32 index=0;index<source->rings.size();++index) {
        ex.spend(1);
        if (rings[index]==kNoGeometryIndex) continue;
        const auto& r=source->rings[index];uses.clear();
        for (u32 k=0;k<r.count;++k) {
            ex.spend(1);
            auto use=source->uses[r.begin+k];const auto& edge=source->edges[use.edge];
            if (edges[use.edge]==kNoGeometryIndex) {
                for (const auto vertex:{edge.first,edge.second})
                    if (points[vertex]==kNoGeometryIndex) points[vertex]=builder.point(source->points[vertex]);
                const auto a=points[edge.first],b=points[edge.second];
                edges[use.edge]=builder.edge(std::min(a,b),std::max(a,b),ancestry(*source,use.edge));
            }
            const auto a=points[use.reversed?edge.second:edge.first],b=points[use.reversed?edge.first:edge.second];
            uses.push_back({edges[use.edge],a>b});
        }
        auto parent=r.parent;
        while (parent!=kNoGeometryIndex && rings[parent]==kNoGeometryIndex) { ex.spend(1);parent=source->rings[parent].parent; }
        rings[index]=builder.ring(uses,parent==kNoGeometryIndex?kNoGeometryIndex:rings[parent]);
    }
    for (u32 i=0;i<source->regions.size();++i) {
        ex.spend(1);if(regions[i]==kNoGeometryIndex)continue;
        holes.clear();neighbors.clear();
        for (const auto h:source->regionHoles(i)) { ex.spend(1);holes.push_back(rings[h]); }
        for (const auto n:source->regionNeighbors(i)) {
            ex.spend(1);if(regions[static_cast<u32>(n)]!=kNoGeometryIndex)neighbors.push_back(static_cast<i32>(regions[static_cast<u32>(n)]));
        }
        const auto& r=source->regions[i];builder.region(rings[r.outer],r.sourceId,holes,neighbors);
    }
    return std::move(builder).finish();
}

bool stepRegions(Execution& ex,Instruction ins) {
    switch(ins.opcode) {
    case Opcode::RegionsDiagram:
        ex.push(Regions2dValue{geometryRegions(ex,ex.popHandle<Diagram2dValue>("diagram2d").data)});break;
    case Opcode::RegionsContours:
        ex.push(Regions2dValue{geometryRegions(ex,ex.popHandle<Contours2dValue>("contours2d").data)});break;
    case Opcode::RegionsCount: {
        const auto r=ex.popHandle<Regions2dValue>("regions2d");ex.push(static_cast<i32>(r.data->regions.size()));break;
    }
    case Opcode::RegionsId: case Opcode::RegionsArea: case Opcode::RegionsCenter: case Opcode::RegionsNeighbors: {
        const auto index=ex.popI32();const auto rs=ex.popHandle<Regions2dValue>("regions2d");
        if(index<0 || static_cast<std::size_t>(index)>=rs.data->regions.size())ex.trap("regions index out of range",ErrorReason::IndexRange);
        const auto& r=rs.data->regions[static_cast<u32>(index)];
        if(ins.opcode==Opcode::RegionsId)ex.push(r.sourceId);
        else if(ins.opcode==Opcode::RegionsArea)ex.push(r.area);
        else if(ins.opcode==Opcode::RegionsCenter)ex.push(r.center);
        else {
            const auto neighbors=rs.data->regionNeighbors(static_cast<u32>(index));ex.spend(neighbors.size());
            ex.allocateResource(ResourceArena::listBytes(neighbors.size()));
            auto data=std::make_shared<ListData>();data->type=ValueType::list(ValueType::I32);data->elements.reserve(neighbors.size());
            for(const auto n:neighbors)data->elements.emplace_back(n);
            ex.resources.lists.push_back(data);ex.push(ListValue{data->type,std::move(data)});
        }
        break;
    }
    default:return false;
    }
    ++ex.frame().ip;return true;
}
} // namespace iprop::detail
