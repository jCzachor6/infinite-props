#include "iprop/Regions.hpp"
#include "iprop/GeometryAlgorithms.hpp"

#include <cmath>
#include <limits>
#include <tuple>

namespace iprop::detail {
namespace {
struct Crossing {double x;i32 delta;u32 edge;};
struct Interval {double left,right;};
struct Bounds {Vec2 lo,hi;};
[[noreturn]] void invalid(Execution& ex,const char* message){ex.trap(message,ErrorReason::NumericRange);}
}
MaskValue rasterRegions(Execution& ex,const GeometryData& source,i32 index){
    if(index < -1 || (index>=0 && static_cast<std::size_t>(index)>=source.regions.size()))
        ex.trap("regions index out of range",ErrorReason::IndexRange);
    const Vec2 scale{ex.canvas().scaleX,ex.canvas().scaleY};
    if(!std::isfinite(scale.x)||!std::isfinite(scale.y)||scale.x<=0||scale.y<=0)
        invalid(ex,"surface raster scale must be finite and positive");
    ex.resources.resourceLifetime()->location=ex.activeInstruction;
    const ResourceReservation workspace(ex.resources,1024);
    ResourceVector<Bounds,16> bounds{ResourceAllocator<Bounds,16>(ex.resources)};
    const u32 first=index<0?0:static_cast<u32>(index),last=index<0?static_cast<u32>(source.regions.size()):first+1;
    Vec2 lo{},hi{};bool nonempty=false;
    for(u32 i=first;i<last;++i){
        const auto& r=source.rings[source.regions[i].outer];Bounds b{source.start(source.uses[r.begin]),source.start(source.uses[r.begin])};
        for(u32 k=0;k<r.count;++k){ex.spend(1);const auto p=source.start(source.uses[r.begin+k]);
            b.lo={std::min(b.lo.x,p.x),std::min(b.lo.y,p.y)};b.hi={std::max(b.hi.x,p.x),std::max(b.hi.y,p.y)};}
        bounds.push_back(b);
        if(!nonempty){lo=b.lo;hi=b.hi;nonempty=true;}
        else{lo={std::min(lo.x,b.lo.x),std::min(lo.y,b.lo.y)};hi={std::max(hi.x,b.hi.x),std::max(hi.y,b.hi.y)};}
    }
    const auto coordinate=[&](f32 p,f32 s,bool upper)->i64{
        const double value=static_cast<double>(p)*s,rounded=upper?std::ceil(value):std::floor(value);
        if(!std::isfinite(rounded)||rounded<std::numeric_limits<i32>::min()||
           rounded>static_cast<double>(std::numeric_limits<i32>::max())+(upper?1:0))invalid(ex,"region mask mapping is not representable");
        return static_cast<i64>(rounded);
    };
    const i64 left=nonempty?coordinate(lo.x,scale.x,false):0,top=nonempty?coordinate(lo.y,scale.y,false):0;
    const i64 right=nonempty?std::max(left+1,coordinate(hi.x,scale.x,true)):1;
    const i64 bottom=nonempty?std::max(top+1,coordinate(hi.y,scale.y,true)):1;
    if(right-left>std::numeric_limits<i32>::max()||bottom-top>std::numeric_limits<i32>::max())invalid(ex,"region mask extent is not representable");
    const auto width=static_cast<u32>(right-left),height=static_cast<u32>(bottom-top);
    const u64 count=static_cast<u64>(width)*height;ex.spend(count);
    ex.allocateResource(maskBytes(width,height));auto buffer=ex.resources.makeSurface(maskBytes(width,height));
    buffer->format=SurfaceBuffer::Format::A8;buffer->raster.width=width;buffer->raster.height=height;
    buffer->raster.scaleX=scale.x;buffer->raster.scaleY=scale.y;buffer->coverage.assign(static_cast<std::size_t>(count),0);
    ResourceVector<Crossing,24> crossings{ResourceAllocator<Crossing,24>(ex.resources)};
    ResourceVector<Interval,16> intervals{ResourceAllocator<Interval,16>(ex.resources)};
    if(nonempty)for(u32 y=0;y<height;++y)for(u32 sy=0;sy<4;++sy){
        const double py=(static_cast<double>(top)+y+(static_cast<double>(sy)+0.5)/4)/scale.y;
        intervals.clear();
        for(u32 i=first;i<last;++i){
            ex.spend(1);const auto b=bounds[i-first];if(py<b.lo.y||py>=b.hi.y)continue;
            crossings.clear();const auto& region=source.regions[i];
            const auto ring=[&](u32 indexOfRing){
                const auto& r=source.rings[indexOfRing];
                for(u32 k=0;k<r.count;++k){
                    ex.spend(1);const auto use=source.uses[r.begin+k];const auto a=source.start(use),b=source.end(use);
                    if((a.y>py)==(b.y>py))continue;
                    // Compute the same crossing from a canonical low/high edge
                    // in either ring direction, including shared diagram edges.
                    const auto low=a.y<b.y?a:b,high=a.y<b.y?b:a;
                    const double x=low.x+(py-low.y)*(static_cast<double>(high.x)-low.x)/(static_cast<double>(high.y)-low.y);
                    crossings.push_back({x,a.y>b.y?1:-1,use.edge});
                }
            };
            ring(region.outer);for(const auto h:source.regionHoles(i))ring(h);
            geometrySort(ex,crossings,[](const auto& a,const auto& b){return std::tuple(a.x,a.delta,a.edge)<std::tuple(b.x,b.delta,b.edge);});
            i32 winding=0;double start=0;
            for(std::size_t at=0;at<crossings.size();){
                const auto x=crossings[at].x;const bool was=winding>0;
                do{ex.spend(1);winding+=crossings[at++].delta;}while(at<crossings.size()&&crossings[at].x==x);
                if(!was&&winding>0)start=x;else if(was&&winding<=0&&x>start)intervals.push_back({start,x});
            }
            if(winding!=0)invalid(ex,"region raster scanline is not closed");
        }
        geometrySort(ex,intervals,[](const auto& a,const auto& b){return std::pair(a.left,a.right)<std::pair(b.left,b.right);});
        std::size_t merged=0;
        for(const auto interval:intervals){ex.spend(1);
            if(merged&&interval.left<=intervals[merged-1].right)intervals[merged-1].right=std::max(intervals[merged-1].right,interval.right);
            else intervals[merged++]=interval;}
        intervals.resize(merged);std::size_t at=0;ex.spend(static_cast<u64>(width)*4);
        for(u32 x=0;x<width;++x){u8 covered=0;
            for(u32 sx=0;sx<4;++sx){
                const double px=(static_cast<double>(left)+x+(static_cast<double>(sx)+0.5)/4)/scale.x;
                while(at<intervals.size()&&px>=intervals[at].right){ex.spend(1);++at;}
                if(at<intervals.size()&&px>=intervals[at].left)++covered;
            }
            buffer->coverage[static_cast<std::size_t>(y)*width+x]+=covered;
        }
    }
    ex.spend(count);for(auto& value:buffer->coverage)value=static_cast<u8>((static_cast<u32>(value)*255+8)/16);
    return{std::move(buffer),{{},{static_cast<i32>(left),static_cast<i32>(top)}}};
}
} // namespace iprop::detail
