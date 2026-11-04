#include "iprop/DynamicParts.hpp"
#include "iprop/VmInternals.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <type_traits>

namespace iprop::detail {
namespace {
void checkCreations(Execution& ex, u32 count) {
    if (ex.dynamicPartCreations > kMaxDynamicParts || count > kMaxDynamicParts-ex.dynamicPartCreations)
        ex.trap("dynamic part creations exceed 256", ErrorReason::RuntimeViolation);
}
void assigned(Execution& ex, bool valid, const char* name) {
    if (!valid) ex.trap(std::string(name)+" is unassigned", ErrorReason::RuntimeViolation);
}
Vec2 targetScale(Execution& ex) {
    const Vec2 scale{ex.canvas().scaleX,ex.canvas().scaleY};
    if(!std::isfinite(scale.x)||!std::isfinite(scale.y)||scale.x<=0||scale.y<=0)
        ex.trap("surface raster scale must be finite and positive",ErrorReason::NumericRange);
    return scale;
}
void locate(Execution& ex){ex.resources.resourceLifetime()->location=ex.activeInstruction;}
struct Point {double x,y;};
Point linear(D4Transform t,Point p){
    // Obtain the linear basis from the existing D4, without its rectangle's
    // extent translation or any f32 conversion of integer pixel coordinates.
    const auto a=t.mapPixelWide({1,0},{1,1}),b=t.mapPixelWide({0,1},{1,1});
    return {a.x*p.x+b.x*p.y,a.y*p.x+b.y*p.y};
}
void metadata(Execution& ex,DynamicPartData& p,Vec2 scale){
    const auto& f=p.original;
    const auto a=linear(p.transform,{f.left,f.top}),b=linear(p.transform,{f.right,f.bottom});
    const auto c=linear(p.transform,{f.centerX,f.centerY});
    p.center={static_cast<f32>((c.x+p.translation.x)/scale.x),static_cast<f32>((c.y+p.translation.y)/scale.y)};
    p.boundsOrigin={static_cast<f32>((std::min(a.x,b.x)+p.translation.x)/scale.x),
                    static_cast<f32>((std::min(a.y,b.y)+p.translation.y)/scale.y)};
    p.boundsSize={static_cast<f32>(std::abs(a.x-b.x)/scale.x),static_cast<f32>(std::abs(a.y-b.y)/scale.y)};
    for(const auto v:{p.center.x,p.center.y,p.boundsOrigin.x,p.boundsOrigin.y,p.boundsSize.x,p.boundsSize.y})
        { ex.work(); if(!std::isfinite(v))ex.trap("dynamic part bounds are not representable",ErrorReason::NumericRange); }
}
std::shared_ptr<DynamicPartData> regionPart(Execution& ex, const Regions2dValue& source, u32 index, i32 id) {
    const auto& region=source.data->regions[index];
    const auto scale=targetScale(ex);
    DynamicPartFrame f;f.scale=scale;f.centerX=static_cast<double>(region.center.x)*scale.x;
    f.centerY=static_cast<double>(region.center.y)*scale.y;
    const auto& ring=source.data->rings[region.outer];ex.spend(ring.count);
    for(u32 k=0;k<ring.count;++k){ ex.work(); const auto p=source.data->start(source.data->uses[ring.begin+k]);
        const double x=static_cast<double>(p.x)*scale.x,y=static_cast<double>(p.y)*scale.y;
        if(k==0){f.left=f.right=x;f.top=f.bottom=y;}
        else{f.left=std::min(f.left,x);f.right=std::max(f.right,x);f.top=std::min(f.top,y);f.bottom=std::max(f.bottom,y);}}
    auto p=makeResource<DynamicPartData,256>(ex.resources);
    p->source=RegionSource{source.data,index};p->id=id;p->sourceId=region.sourceId;
    p->center=region.center;p->boundsOrigin=region.boundsOrigin;p->boundsSize=region.boundsSize;
    p->original=f;
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
    for(u32 y=0;y<raster.height;++y){ ex.work(); for(u32 x=0;x<raster.width;++x){
        ex.work();
        const auto at=static_cast<std::size_t>(y)*raster.width+x;
        const auto alpha=source.buffer->format==SurfaceBuffer::Format::A8?source.buffer->coverage[at]:raster.pixels[at].a;
        if(alpha){loX=std::min(loX,x);loY=std::min(loY,y);hiX=std::max(hiX,x);hiY=std::max(hiY,y);}
    } }
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
        { ex.work(); if(!std::isfinite(v))ex.trap("dynamic part bounds are not representable",ErrorReason::NumericRange); }
    locate(ex);auto p=makeResource<DynamicPartData,256>(ex.resources);
    p->source=source;p->id=static_cast<i32>(ex.dynamicPartCreations);
    p->center=center;p->boundsOrigin=origin;p->boundsSize=size;
    p->original={left+width*0.5,top+height*0.5,left,top,left+width,top+height,scale};
    ++ex.dynamicPartCreations;return {std::move(p)};
}
}
Part2dValue createPart(Execution& ex,const Regions2dValue& source,i32 index) {
    assigned(ex,static_cast<bool>(source.data),"regions2d");
    if(index<0||static_cast<std::size_t>(index)>=source.data->regions.size())
        ex.trap("regions index out of range",ErrorReason::IndexRange);
    checkCreations(ex,1);ex.spend(1);locate(ex);
    auto p=regionPart(ex,source,static_cast<u32>(index),static_cast<i32>(ex.dynamicPartCreations));
    ++ex.dynamicPartCreations;return {std::move(p)};
}
Part2dValue createPart(Execution& ex,const MaskValue& source){return rasterPart(ex,source);}
Part2dValue createPart(Execution& ex,const LayerValue& source){return rasterPart(ex,source);}
Parts2dValue partsFromRegions(Execution& ex,const Regions2dValue& source) {
    assigned(ex,static_cast<bool>(source.data),"regions2d");
    const auto count=static_cast<u32>(source.data->regions.size());checkCreations(ex,count);ex.spend(count);locate(ex);
    auto result=makeResource<DynamicPartsData,128>(ex.resources,ex.resources);result->parts.reserve(count);
    for(u32 i=0;i<count;++i){ ex.work(); result->parts.push_back({regionPart(ex,source,i,static_cast<i32>(ex.dynamicPartCreations+i))}); }
    ex.dynamicPartCreations+=count;return {std::move(result)};
}
SurfaceMapping partSurfaceMapping(Execution& ex,const DynamicPartData& p,const SurfaceBuffer& source,const SurfaceMapping& mapping){
    const auto extent=mapping.transform.mapExtent(RasterPoint{static_cast<i32>(source.raster.width),static_cast<i32>(source.raster.height)});
    const auto origin=p.transform.mapPixelWide({mapping.origin.x,mapping.origin.y},{1,1});
    const auto end=p.transform.mapPixelWide({extent.x,extent.y},{1,1});
    const i64 x=origin.x+p.translation.x+std::min(i64{0},end.x),y=origin.y+p.translation.y+std::min(i64{0},end.y);
    if(x<std::numeric_limits<i32>::min()||x>std::numeric_limits<i32>::max()||
       y<std::numeric_limits<i32>::min()||y>std::numeric_limits<i32>::max())
        ex.trap("dynamic part mapping is not representable",ErrorReason::NumericRange);
    const SurfaceMapping result{mapping.transform.compose(p.transform),{static_cast<i32>(x),static_cast<i32>(y)}};
    validateSurfaceView(ex,source,result,true);return result;
}
Part2dValue placePart(Execution& ex,const Part2dValue& part,Vec2 at,i32 rot,i32 mirrorX,i32 mirrorY){
    assigned(ex,static_cast<bool>(part.data),"part2d");
    const auto t=D4Transform::fromParameters(rot,mirrorX,mirrorY);
    if(!t)ex.trap("dynamic part requires rot 0/90/180/270 and mirror flags 0/1",ErrorReason::NumericRange);
    if(!std::isfinite(at.x)||!std::isfinite(at.y))ex.trap("dynamic part position must be finite",ErrorReason::NumericRange);
    const auto scale=targetScale(ex);
    const bool geometry=std::holds_alternative<RegionSource>(part.data->source);
    if(!geometry&&t->mapExtent(part.data->original.scale)!=scale)
        ex.trap("surface raster scale does not match the active target",ErrorReason::NumericRange);
    DynamicPartData result=*part.data;result.transform=*t;result.placed=true;
    if(geometry){
        // Vector geometry has no intrinsic pixel scale. Cache the original
        // frame in the grid which becomes the active target after D4. Each
        // cached product was f32*f32 in binary64, so division recovers the
        // exact original local coordinate without rescanning/copying vertices.
        const auto needed=t->mapExtent(scale);auto& frame=result.original;
        if(needed!=frame.scale){
            frame.centerX=frame.centerX/frame.scale.x*needed.x;frame.centerY=frame.centerY/frame.scale.y*needed.y;
            frame.left=frame.left/frame.scale.x*needed.x;frame.right=frame.right/frame.scale.x*needed.x;
            frame.top=frame.top/frame.scale.y*needed.y;frame.bottom=frame.bottom/frame.scale.y*needed.y;frame.scale=needed;
        }
        if(result.texture&&result.texture->mapping.transform.mapExtent(Vec2{result.texture->buffer->raster.scaleX,result.texture->buffer->raster.scaleY})!=needed)
            ex.trap("texture raster scale does not match the placed part source grid",ErrorReason::NumericRange);
    }
    const auto anchor=linear(*t,{result.original.centerX,result.original.centerY});
    const auto rounded=[&](double value){const auto n=std::floor(value+0.5);
        if(!std::isfinite(n)||n<std::numeric_limits<i32>::min()||n>std::numeric_limits<i32>::max())
            ex.trap("dynamic part translation is not representable",ErrorReason::NumericRange);
        return static_cast<i32>(n);};
    result.translation={rounded(static_cast<double>(at.x)*scale.x-anchor.x),rounded(static_cast<double>(at.y)*scale.y-anchor.y)};
    metadata(ex,result,scale);
    std::visit([&](const auto& source){if constexpr(!std::is_same_v<std::decay_t<decltype(source)>,RegionSource>)
        (void)partSurfaceMapping(ex,result,*source.buffer,source.mapping);},result.source);
    locate(ex);return {makeResource<DynamicPartData,256>(ex.resources,std::move(result))};
}
Part2dValue fillPart(Execution& ex,const Part2dValue& part,Color tint){
    assigned(ex,static_cast<bool>(part.data),"part2d");locate(ex);
    auto p=makeResource<DynamicPartData,256>(ex.resources,*part.data);p->fill=tint;p->texture.reset();return {std::move(p)};
}
Part2dValue texturePart(Execution& ex,const Part2dValue& part,const LayerValue& image){
    assigned(ex,static_cast<bool>(part.data),"part2d");assigned(ex,static_cast<bool>(image.buffer),"layer");
    if(image.buffer->format!=SurfaceBuffer::Format::RGBA8)ex.trap("layer buffer must be RGBA8",ErrorReason::RuntimeViolation);
    validateSurfaceView(ex,*image.buffer,image.mapping,false);
    if((!std::holds_alternative<RegionSource>(part.data->source)||part.data->placed)&&
        image.mapping.transform.mapExtent(Vec2{image.buffer->raster.scaleX,image.buffer->raster.scaleY})!=part.data->original.scale)
        ex.trap("texture raster scale does not match the original part source",ErrorReason::NumericRange);
    locate(ex);auto p=makeResource<DynamicPartData,256>(ex.resources,*part.data);p->texture=image;p->fill.reset();return {std::move(p)};
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
    case Opcode::PartCenter:case Opcode::PartBoundsOrigin:case Opcode::PartBoundsSize:{const auto p=ex.popHandle<Part2dValue>("part2d");
        ex.push(ins.opcode==Opcode::PartCenter?p.data->center:ins.opcode==Opcode::PartBoundsOrigin?p.data->boundsOrigin:p.data->boundsSize);break;}
    case Opcode::PartPlace:{const auto my=ex.popI32(),mx=ex.popI32(),rot=ex.popI32();const auto at=ex.popVec2();const auto p=ex.popHandle<Part2dValue>("part2d");
        ex.push(placePart(ex,p,at,rot,mx,my));break;}
    case Opcode::PartFill:{const auto tint=ex.popColor();const auto p=ex.popHandle<Part2dValue>("part2d");ex.push(fillPart(ex,p,tint));break;}
    case Opcode::PartTexture:{const auto image=ex.popLayer();const auto p=ex.popHandle<Part2dValue>("part2d");ex.push(texturePart(ex,p,image));break;}
    case Opcode::DynamicPartDraw:{const auto p=ex.popHandle<Part2dValue>("part2d");drawPart(ex,p);break;}
    case Opcode::PartMask:{const auto p=ex.popHandle<Part2dValue>("part2d");ex.push(maskPart(ex,p));break;}
    default:return false;
    }
    ++ex.frame().ip;return true;
}
} // namespace iprop::detail
