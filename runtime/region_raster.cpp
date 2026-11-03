#include "iprop/Regions.hpp"
#include "iprop/GeometryAlgorithms.hpp"

#include <cmath>
#include <array>
#include <limits>
#include <tuple>

namespace iprop::detail {
namespace {
struct Crossing {double x;i32 delta;u32 edge;};
struct Interval {double left,right;};
struct Bounds {Vec2 lo,hi;};
[[noreturn]] void invalid(Execution& ex,const char* message){ex.trap(message,ErrorReason::NumericRange);}
struct RasterResult {std::shared_ptr<const SurfaceBuffer> buffer;SurfaceMapping mapping;};
struct ShadeEdge {Vec2 a,b,lo,hi;double length2=0,nx=0,ny=0;u32 original=0;};
struct ShadePixel {double a=0,r=0,g=0,b=0;};
struct Shade {
    Execution& ex;double width,lightX,lightY,strength;Color highlight,shadow;
    ResourceVector<ShadeEdge,64> edges;
    ResourceVector<u32,4> active;
    ResourceVector<u16,2> owners;
    ResourceVector<ShadePixel,32> row;
    std::array<std::pair<u32,u32>,256> ranges{};
    Shade(Execution& execution,f32 w,Vec2 light,Color hi,Color lo,f32 s)
        :ex(execution),width(w),strength(s),highlight(hi),shadow(lo),
         edges(ResourceAllocator<ShadeEdge,64>(ex.resources)),active(ResourceAllocator<u32,4>(ex.resources)),
         owners(ResourceAllocator<u16,2>(ex.resources)),row(ResourceAllocator<ShadePixel,32>(ex.resources)){
        const double x=light.x,y=light.y,length=std::sqrt(x*x+y*y);lightX=x/length;lightY=y/length;
    }
    void initialize(const GeometryData& source){
        ex.spend(source.uses.size()*4);edges.reserve(source.uses.size());
        for(const auto use:source.uses){const auto a=source.start(use),b=source.end(use);
            const double dx=static_cast<double>(b.x)-a.x,dy=static_cast<double>(b.y)-a.y,l2=dx*dx+dy*dy,l=std::sqrt(l2);
            edges.push_back({a,b,{std::min(a.x,b.x),std::min(a.y,b.y)},{std::max(a.x,b.x),std::max(a.y,b.y)},
                l2,dy/l,-dx/l,source.source?source.edges[use.edge].provenance.edge:use.edge});
        }
    }
    void prepare(u32 pixels){ex.spend(static_cast<u64>(pixels)*5);owners.resize(static_cast<std::size_t>(pixels)*4);row.resize(pixels);}
    void beginRow(u32 pixels){ex.spend(pixels);std::fill(row.begin(),row.end(),ShadePixel{});}
    void beginSubrow(){ex.spend(owners.size());std::fill(owners.begin(),owners.end(),u16{0xffff});active.clear();}
    void edge(u32 at,double py){const auto& e=edges[at];
        if(py>=static_cast<double>(e.lo.y)-width&&py<=static_cast<double>(e.hi.y)+width){ex.spend(1);active.push_back(at);}}
    void interval(double left,double right,u32 region,i64 origin,f32 scale){
        const auto lower=[&](double edge){std::size_t lo=0,hi=owners.size();while(lo<hi){
            ex.spend(1);const auto mid=(lo+hi)/2;const double px=(static_cast<double>(origin)+(static_cast<double>(mid)+0.5)/4)/scale;
            if(px<edge)lo=mid+1;else hi=mid;}return lo;};
        const auto end=lower(right);for(auto at=lower(left);at<end;++at){ex.spend(1);if(owners[at]==0xffff)owners[at]=static_cast<u16>(region);}
    }
    void sample(double px,double py,u16 region,ShadePixel& sum){
        double nearest=width*width;u32 winner=kNoGeometryIndex;
        const auto [first,last]=ranges[region];
        for(u32 k=first;k<last;++k){ex.spend(1);const auto at=active[k];const auto& e=edges[at];
            if(px<static_cast<double>(e.lo.x)-width||px>static_cast<double>(e.hi.x)+width)continue;
            ex.spend(4);const double dx=static_cast<double>(e.b.x)-e.a.x,dy=static_cast<double>(e.b.y)-e.a.y;
            const double t=std::clamp(((px-e.a.x)*dx+(py-e.a.y)*dy)/e.length2,0.0,1.0);
            const double x=px-e.a.x-dx*t,y=py-e.a.y-dy*t,d2=x*x+y*y;
            if(d2<nearest||(d2==nearest&&(winner==kNoGeometryIndex||std::pair(e.original,at)<std::pair(edges[winner].original,winner)))){nearest=d2;winner=at;}
        }
        if(winner==kNoGeometryIndex)return;
        ex.spend(4);const auto& e=edges[winner];const double dot=std::clamp(e.nx*lightX+e.ny*lightY,-1.0,1.0);
        const auto color=dot>=0?highlight:shadow;const double fade=std::clamp(1-std::sqrt(nearest)/width,0.0,1.0);
        const double alpha=static_cast<double>(color.a)*strength*std::abs(dot)*fade;
        sum.a+=alpha;sum.r+=alpha*color.r;sum.g+=alpha*color.g;sum.b+=alpha*color.b;
    }
    static u8 quantize(double value){return static_cast<u8>(std::clamp(std::floor(value+0.5),0.0,255.0));}
    void finishRow(Canvas& canvas,u32 y){ex.spend(row.size());for(u32 x=0;x<row.size();++x){const auto& p=row[x];const auto alpha=quantize(p.a/16);
        canvas.at(x,y)=alpha?Color{quantize(p.r/p.a),quantize(p.g/p.a),quantize(p.b/p.a),alpha}:Color{};}}
};
}
namespace {
RasterResult rasterRegionsCore(Execution& ex,const GeometryData& source,i32 index,Shade* shade){
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
    const u64 count=static_cast<u64>(width)*height;ex.spend(count*(shade?4:1));
    const auto bytes=shade?surfaceBytes(width,height):maskBytes(width,height);
    ex.allocateResource(bytes);auto buffer=ex.resources.makeSurface(bytes);
    buffer->format=shade?SurfaceBuffer::Format::RGBA8:SurfaceBuffer::Format::A8;buffer->raster.width=width;buffer->raster.height=height;
    buffer->raster.scaleX=scale.x;buffer->raster.scaleY=scale.y;
    if(shade)buffer->raster.pixels.resize(static_cast<std::size_t>(count));else buffer->coverage.assign(static_cast<std::size_t>(count),0);
    if(shade&&(!nonempty||shade->strength==0))return{std::move(buffer),{{},{static_cast<i32>(left),static_cast<i32>(top)}}};
    if(shade){shade->initialize(source);shade->prepare(width);}
    ResourceVector<Crossing,24> crossings{ResourceAllocator<Crossing,24>(ex.resources)};
    ResourceVector<Interval,16> intervals{ResourceAllocator<Interval,16>(ex.resources)};
    if(nonempty)for(u32 y=0;y<height;++y)for(u32 sy=0;sy<4;++sy){
        if(shade){if(sy==0)shade->beginRow(width);shade->beginSubrow();}
        const double py=(static_cast<double>(top)+y+(static_cast<double>(sy)+0.5)/4)/scale.y;
        intervals.clear();
        for(u32 i=first;i<last;++i){
            if(shade)shade->ranges[i]={static_cast<u32>(shade->active.size()),static_cast<u32>(shade->active.size())};
            ex.spend(1);const auto b=bounds[i-first];if(py<b.lo.y||py>=b.hi.y)continue;
            crossings.clear();const auto& region=source.regions[i];
            const auto ring=[&](u32 indexOfRing){
                const auto& r=source.rings[indexOfRing];
                for(u32 k=0;k<r.count;++k){
                    ex.spend(1);const auto use=source.uses[r.begin+k];const auto a=source.start(use),b=source.end(use);
                    if(shade)shade->edge(r.begin+k,py);
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
                if(!was&&winding>0)start=x;else if(was&&winding<=0&&x>start){
                    if(shade)shade->interval(start,x,i,left,scale.x);else intervals.push_back({start,x});}
            }
            if(winding!=0)invalid(ex,"region raster scanline is not closed");
            if(shade)shade->ranges[i].second=static_cast<u32>(shade->active.size());
        }
        if(shade){
            ex.spend(static_cast<u64>(width)*4);
            for(u32 x=0;x<width;++x)for(u32 sx=0;sx<4;++sx){const auto owner=shade->owners[static_cast<std::size_t>(x)*4+sx];
                if(owner==0xffff)continue;
                const double px=(static_cast<double>(left)+x+(static_cast<double>(sx)+0.5)/4)/scale.x;
                shade->sample(px,py,owner,shade->row[x]);}
            if(sy==3)shade->finishRow(buffer->raster,y);continue;
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
    if(!shade){ex.spend(count);for(auto& value:buffer->coverage)value=static_cast<u8>((static_cast<u32>(value)*255+8)/16);}
    return{std::move(buffer),{{},{static_cast<i32>(left),static_cast<i32>(top)}}};
}
}
MaskValue rasterRegions(Execution& ex,const GeometryData& source,i32 index){
    const auto result=rasterRegionsCore(ex,source,index,nullptr);return{result.buffer,result.mapping};
}
LayerValue bevelRegions(Execution& ex,const GeometryData& source,f32 width,Vec2 light,Color highlight,Color shadow,f32 strength){
    if(!std::isfinite(width)||width<=0)invalid(ex,"regions bevel width must be finite and positive");
    if(!std::isfinite(light.x)||!std::isfinite(light.y)||(light.x==0&&light.y==0))invalid(ex,"regions bevel light must be finite and nonzero");
    if(!std::isfinite(strength)||strength<0||strength>1)invalid(ex,"regions bevel strength must be finite in [0,1]");
    if(!std::isfinite(ex.canvas().scaleX)||!std::isfinite(ex.canvas().scaleY)||ex.canvas().scaleX<=0||ex.canvas().scaleY<=0)
        invalid(ex,"surface raster scale must be finite and positive");
    ex.resources.resourceLifetime()->location=ex.activeInstruction;const ResourceReservation workspace(ex.resources,4096);
    Shade shade(ex,width,light,highlight,shadow,strength);
    const auto result=rasterRegionsCore(ex,source,-1,&shade);return{result.buffer,result.mapping};
}
} // namespace iprop::detail
