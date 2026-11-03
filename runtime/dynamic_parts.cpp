#include "iprop/DynamicParts.hpp"
#include "iprop/VmInternals.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace iprop::detail {
namespace {
void checkCreations(Execution& ex, u32 count) {
    if (ex.dynamicPartCreations > kMaxDynamicParts || count > kMaxDynamicParts-ex.dynamicPartCreations)
        ex.trap("dynamic part creations exceed 256", ErrorReason::RuntimeViolation);
}
void assigned(Execution& ex, bool valid, const char* name) {
    if (!valid) ex.trap(std::string(name)+" is unassigned", ErrorReason::RuntimeViolation);
}
std::shared_ptr<DynamicPartData> regionPart(Execution& ex, const Regions2dValue& source, u32 index, i32 id) {
    const auto& region=source.data->regions[index];
    auto p=makeResource<DynamicPartData,256>(ex.resources);
    p->source=RegionSource{source.data,index};p->id=id;p->sourceId=region.sourceId;
    p->center=region.center;p->boundsOrigin=region.boundsOrigin;p->boundsSize=region.boundsSize;
    return p;
}
template<class Surface> Part2dValue rasterPart(Execution& ex, const Surface& source) {
    assigned(ex,static_cast<bool>(source.buffer),"dynamic part source");
    // Masks may be zero-copy alpha views of RGBA, just like mask.from(layer).
    if constexpr (std::is_same_v<Surface,LayerValue>) {
        if(source.buffer->format!=SurfaceBuffer::Format::RGBA8)ex.trap("layer buffer must be RGBA8",ErrorReason::RuntimeViolation);
    }
    validateSurfaceView(ex,*source.buffer,source.mapping,true);checkCreations(ex,1);
    const auto& raster=source.buffer->raster;
    ex.spend(static_cast<u64>(raster.width)*raster.height);
    u32 loX=raster.width,loY=raster.height,hiX=0,hiY=0;
    for(u32 y=0;y<raster.height;++y)for(u32 x=0;x<raster.width;++x){
        const auto at=static_cast<std::size_t>(y)*raster.width+x;
        const auto alpha=source.buffer->format==SurfaceBuffer::Format::A8?source.buffer->coverage[at]:raster.pixels[at].a;
        if(alpha){loX=std::min(loX,x);loY=std::min(loY,y);hiX=std::max(hiX,x);hiY=std::max(hiY,y);}
    }
    if(loX==raster.width)ex.trap("empty dynamic part",ErrorReason::RuntimeViolation);
    const RasterPoint extent{static_cast<i32>(raster.width),static_cast<i32>(raster.height)};
    const auto a=source.mapping.transform.mapPixelWide({loX,loY},extent);
    const auto b=source.mapping.transform.mapPixelWide({hiX,hiY},extent);
    const auto scale=source.mapping.transform.mapExtent(Vec2{raster.scaleX,raster.scaleY});
    const double left=static_cast<double>(source.mapping.origin.x)+std::min(a.x,b.x);
    const double top=static_cast<double>(source.mapping.origin.y)+std::min(a.y,b.y);
    const double width=static_cast<double>(std::max(a.x,b.x)-std::min(a.x,b.x)+1);
    const double height=static_cast<double>(std::max(a.y,b.y)-std::min(a.y,b.y)+1);
    const Vec2 origin{static_cast<f32>(left/scale.x),static_cast<f32>(top/scale.y)};
    const Vec2 size{static_cast<f32>(width/scale.x),static_cast<f32>(height/scale.y)};
    const Vec2 center{static_cast<f32>((left+width*0.5)/scale.x),static_cast<f32>((top+height*0.5)/scale.y)};
    for(const auto v:{origin.x,origin.y,size.x,size.y,center.x,center.y})
        if(!std::isfinite(v))ex.trap("dynamic part bounds are not representable",ErrorReason::NumericRange);
    auto p=makeResource<DynamicPartData,256>(ex.resources);
    p->source=source;p->id=static_cast<i32>(ex.dynamicPartCreations);
    p->center=center;p->boundsOrigin=origin;p->boundsSize=size;
    ++ex.dynamicPartCreations;return {std::move(p)};
}
}
Part2dValue createPart(Execution& ex,const Regions2dValue& source,i32 index) {
    assigned(ex,static_cast<bool>(source.data),"regions2d");
    if(index<0||static_cast<std::size_t>(index)>=source.data->regions.size())
        ex.trap("regions index out of range",ErrorReason::IndexRange);
    checkCreations(ex,1);ex.spend(1);
    auto p=regionPart(ex,source,static_cast<u32>(index),static_cast<i32>(ex.dynamicPartCreations));
    ++ex.dynamicPartCreations;return {std::move(p)};
}
Part2dValue createPart(Execution& ex,const MaskValue& source){return rasterPart(ex,source);}
Part2dValue createPart(Execution& ex,const LayerValue& source){return rasterPart(ex,source);}
Parts2dValue partsFromRegions(Execution& ex,const Regions2dValue& source) {
    assigned(ex,static_cast<bool>(source.data),"regions2d");
    const auto count=static_cast<u32>(source.data->regions.size());checkCreations(ex,count);ex.spend(count);
    auto result=makeResource<DynamicPartsData,128>(ex.resources,ex.resources);result->parts.reserve(count);
    for(u32 i=0;i<count;++i)result->parts.push_back({regionPart(ex,source,i,static_cast<i32>(ex.dynamicPartCreations+i))});
    ex.dynamicPartCreations+=count;return {std::move(result)};
}
bool stepDynamicParts(Execution& ex,Instruction ins) {
    switch(ins.opcode){
    case Opcode::PartCreateRegion:{const auto index=ex.popI32();const auto source=ex.popHandle<Regions2dValue>("regions2d");ex.push(createPart(ex,source,index));break;}
    case Opcode::PartCreateMask:{const auto source=ex.popMask();ex.push(createPart(ex,source));break;}
    case Opcode::PartCreateLayer:{const auto source=ex.popLayer();ex.push(createPart(ex,source));break;}
    case Opcode::PartsFromRegions:{const auto source=ex.popHandle<Regions2dValue>("regions2d");ex.push(partsFromRegions(ex,source));break;}
    case Opcode::PartsCount:{const auto source=ex.popHandle<Parts2dValue>("parts2d");ex.push(static_cast<i32>(source.data->parts.size()));break;}
    case Opcode::PartsGet:{const auto index=ex.popI32();const auto source=ex.popHandle<Parts2dValue>("parts2d");
        if(index<0||static_cast<std::size_t>(index)>=source.data->parts.size())ex.trap("parts index out of range",ErrorReason::IndexRange);
        ex.push(source.data->parts[static_cast<u32>(index)]);break;}
    case Opcode::PartId:case Opcode::PartSourceId:{const auto p=ex.popHandle<Part2dValue>("part2d");
        ex.push(ins.opcode==Opcode::PartId?p.data->id:p.data->sourceId);break;}
    default:return false;
    }
    ++ex.frame().ip;return true;
}
} // namespace iprop::detail
