#include "iprop/DynamicParts.hpp"
#include "iprop/Regions.hpp"
#include "iprop/VmInternals.hpp"

#include <type_traits>

namespace iprop::detail {
namespace {
const DynamicPartData& checked(Execution& ex,const Part2dValue& part){
    if(!part.data)ex.trap("part2d is unassigned",ErrorReason::RuntimeViolation);
    const auto& p=*part.data;
    ex.resources.resourceLifetime()->location=ex.activeInstruction;
    if((!std::holds_alternative<RegionSource>(p.source)||p.placed)&&
        p.transform.mapExtent(p.original.scale)!=Vec2{ex.canvas().scaleX,ex.canvas().scaleY})
        ex.trap("surface raster scale does not match the active target",ErrorReason::NumericRange);
    return p;
}
MaskValue originalMask(Execution& ex,const DynamicPartData& p){
    return std::visit([&](const auto& source)->MaskValue{
        if constexpr(std::is_same_v<std::decay_t<decltype(source)>,RegionSource>)
            return rasterRegionsAtScale(ex,*source.geometry,static_cast<i32>(source.region),
                p.placed?p.original.scale:Vec2{ex.canvas().scaleX,ex.canvas().scaleY});
        else return {source.buffer,source.mapping};
    },p.source);
}
Color sampleTexture(const LayerValue& image,i64 x,i64 y){
    const auto& raster=image.buffer->raster;
    const auto extent=image.mapping.transform.mapExtent(RasterPoint{static_cast<i32>(raster.width),static_cast<i32>(raster.height)});
    x-=image.mapping.origin.x;y-=image.mapping.origin.y;
    if(x<0||y<0||x>=extent.x||y>=extent.y)return {};
    const auto point=image.mapping.transform.inverse().mapPixel({static_cast<i32>(x),static_cast<i32>(y)},extent);
    return raster.at(static_cast<u32>(point.x),static_cast<u32>(point.y));
}
}
MaskValue maskPart(Execution& ex,const Part2dValue& part){
    const auto& p=checked(ex,part);auto mask=originalMask(ex,p);
    // Validate the complete source mapping, independently of clipping/target
    // extent, before any materialization. Cropping is contained in this view.
    const auto mapped=partSurfaceMapping(ex,p,*mask.buffer,mask.mapping);
    if(mask.buffer->format==SurfaceBuffer::Format::A8)return {mask.buffer,mapped};
    // RGBA shape alpha is materialized as a compact A8 bbox. A default layer
    // draw takes a separate direct path, so its alpha is never squared.
    const auto& f=p.original;
    const auto left=static_cast<i32>(f.left),top=static_cast<i32>(f.top);
    const auto width=static_cast<u32>(f.right-f.left),height=static_cast<u32>(f.bottom-f.top);
    const u64 count=static_cast<u64>(width)*height;ex.spend(count*2);
    const auto bytes=maskBytes(width,height);ex.allocateResource(bytes);auto buffer=ex.resources.makeSurface(bytes);
    buffer->format=SurfaceBuffer::Format::A8;buffer->raster.width=width;buffer->raster.height=height;
    buffer->raster.scaleX=f.scale.x;buffer->raster.scaleY=f.scale.y;buffer->coverage.resize(static_cast<std::size_t>(count));
    for(u32 y=0;y<height;++y)for(u32 x=0;x<width;++x)
        buffer->coverage[static_cast<std::size_t>(y)*width+x]=maskCoverage(mask,static_cast<i64>(left)+x,static_cast<i64>(top)+y);
    const auto mapping=partSurfaceMapping(ex,p,*buffer,{{},{left,top}});return {std::move(buffer),mapping};
}
void drawPart(Execution& ex,const Part2dValue& part){
    const auto& p=checked(ex,part);
    if(p.texture&&std::holds_alternative<RegionSource>(p.source)){
        const auto needed=p.placed?p.original.scale:Vec2{ex.canvas().scaleX,ex.canvas().scaleY};
        if(p.texture->mapping.transform.mapExtent(Vec2{p.texture->buffer->raster.scaleX,p.texture->buffer->raster.scaleY})!=needed)
            ex.trap("texture raster scale does not match the part source grid",ErrorReason::NumericRange);
    }
    if(!p.fill&&!p.texture){
        const auto* captured=std::get_if<LayerValue>(&p.source);
        if(!captured)ex.trap("dynamic part has no appearance",ErrorReason::RuntimeViolation);
        auto image=*captured;image.mapping=partSurfaceMapping(ex,p,*image.buffer,image.mapping);
        ex.spend(static_cast<u64>(image.buffer->raster.width)*image.buffer->raster.height);
        paintSurface(ex,image);return;
    }
    const auto mask=originalMask(ex,p);const auto mapping=partSurfaceMapping(ex,p,*mask.buffer,mask.mapping);
    const auto& raster=mask.buffer->raster;
    const RasterPoint extent{static_cast<i32>(raster.width),static_cast<i32>(raster.height)};
    ex.spend(static_cast<u64>(raster.width)*raster.height*(p.texture?2:1));
    for(u32 y=0;y<raster.height;++y)for(u32 x=0;x<raster.width;++x){
        const auto at=static_cast<std::size_t>(y)*raster.width+x;
        const auto coverage=mask.buffer->format==SurfaceBuffer::Format::A8?mask.buffer->coverage[at]:raster.pixels[at].a;
        if(!coverage)continue;
        const auto destination=mapping.transform.mapPixel({static_cast<i32>(x),static_cast<i32>(y)},extent);
        const i64 dx=static_cast<i64>(mapping.origin.x)+destination.x,dy=static_cast<i64>(mapping.origin.y)+destination.y;
        if(dx<0||dy<0||dx>=ex.canvas().width||dy>=ex.canvas().height)continue;
        Color color;
        if(p.fill)color=*p.fill;
        else{
            const auto original=mask.mapping.transform.mapPixel({static_cast<i32>(x),static_cast<i32>(y)},extent);
            color=sampleTexture(*p.texture,static_cast<i64>(mask.mapping.origin.x)+original.x,
                static_cast<i64>(mask.mapping.origin.y)+original.y);
        }
        color.a=multiplyCoverage(color.a,coverage);
        compositePixel(ex,static_cast<i32>(dx),static_cast<i32>(dy),color);
    }
}
} // namespace iprop::detail
