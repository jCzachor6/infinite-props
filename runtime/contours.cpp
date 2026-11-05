#include "iprop/Contours.hpp"
#include "iprop/VmInternals.hpp"

#include <array>
#include <bit>
#include <cmath>
#include <limits>
#include <tuple>

namespace iprop::detail {
namespace {
template<class T, u64 Slot> using Buffer = ResourceVector<T, Slot>;
struct Point { double x, y; };
struct Endpoint { u64 key; Point p; };
struct Segment { u64 first, second; Vec2 point; u32 next = 0, discovery = 0; };
struct Vertex { Vec2 p; u32 previous, next, ring; bool alive = true; };
struct Ring {
    u32 begin = 0, rawCount = 0, start = 0, count = 0, discovery = 0;
    u32 parent = kNoGeometryIndex, depth = 0;
    double area = 0, netArea = 0;
    Vec2 lo{}, hi{};
    i32 sourceId = -1;
    bool reversed = false, keep = false;
};
[[noreturn]] void invalid(Execution& ex, const char* text) { ex.trap(text, ErrorReason::NumericRange); }
bool same(Vec2 a, Vec2 b) { return a.x == b.x && a.y == b.y; }
double turn(Vec2 a, Vec2 b, Vec2 c) {
    return (static_cast<double>(b.x)-a.x)*(static_cast<double>(c.y)-a.y)-
           (static_cast<double>(b.y)-a.y)*(static_cast<double>(c.x)-a.x);
}
bool onSegment(Vec2 a, Vec2 b, Vec2 p) {
    return p.x >= std::min(a.x,b.x) && p.x <= std::max(a.x,b.x) &&
           p.y >= std::min(a.y,b.y) && p.y <= std::max(a.y,b.y);
}
bool intersects(Vec2 a, Vec2 b, Vec2 c, Vec2 d) {
    if (std::max(a.x,b.x) < std::min(c.x,d.x) || std::max(c.x,d.x) < std::min(a.x,b.x) ||
        std::max(a.y,b.y) < std::min(c.y,d.y) || std::max(c.y,d.y) < std::min(a.y,b.y)) return false;
    const double ac=turn(a,b,c), ad=turn(a,b,d), ca=turn(c,d,a), cb=turn(c,d,b);
    if ((ac == 0 && onSegment(a,b,c)) || (ad == 0 && onSegment(a,b,d)) ||
        (ca == 0 && onSegment(c,d,a)) || (cb == 0 && onSegment(c,d,b))) return true;
    return ((ac > 0 && ad < 0) || (ac < 0 && ad > 0)) &&
           ((ca > 0 && cb < 0) || (ca < 0 && cb > 0));
}
// Iterative heapsort has fixed automatic workspace and a portable work bound.
template<class T, u64 Slot, class Less>
void sorted(Execution& ex, Buffer<T,Slot>& values, Less less) {
    const auto n=static_cast<u64>(values.size());
    ex.spend(4*n*(1+std::bit_width(n)));
    const auto sift=[&](std::size_t root, std::size_t count) {
        while (root < count/2) {
            ex.work();
            auto child=root*2+1;
            if (child+1 < count && less(values[child],values[child+1])) ++child;
            if (!less(values[root],values[child])) break;
            std::swap(values[root],values[child]); root=child;
        }
    };
    for (auto i=values.size()/2; i>0; --i) { ex.work(); sift(i-1,values.size()); }
    for (auto i=values.size(); i>1; --i) { ex.work();  std::swap(values[0],values[i-1]); sift(0,i-1); }
}

struct Extractor {
    Execution& ex;
    const MaskValue& source;
    const ContourOptions& options;
    Buffer<Segment,40> segments;
    u32 width, height;
    double level;
    Extractor(Execution& e, const MaskValue& s, const ContourOptions& o)
        : ex(e), source(s), options(o), segments(ResourceAllocator<Segment,40>(e.resources)),
          width(s.buffer->raster.width), height(s.buffer->raster.height), level(static_cast<double>(o.threshold)*255) {}
    double coordinate(u32 index, u32 extent) const {
        return index == 0 ? 0 : index == extent+1 ? static_cast<double>(extent) : static_cast<double>(index)-0.5;
    }
    Point point(u32 x, u32 y) const { return {coordinate(x,width),coordinate(y,height)}; }
    u8 alpha(u32 x, u32 y) const {
        const auto ix=std::clamp(x,1u,width)-1, iy=std::clamp(y,1u,height)-1;
        const auto index=static_cast<std::size_t>(iy)*width+ix;
        return source.buffer->format==SurfaceBuffer::Format::A8 ? source.buffer->coverage[index] : source.buffer->raster.pixels[index].a;
    }
    bool inside(u8 a) const { return options.threshold == 0 ? a > 0 : static_cast<double>(a) >= level; }
    Endpoint node(u32 x, u32 y) const {
        return {(u64{2}<<60) + static_cast<u64>(y)*(width+2)+x,point(x,y)};
    }
    Endpoint crossing(u32 x, u32 y, bool vertical) const {
        const auto a=point(x,y), b=point(x+(vertical?0:1),y+(vertical?1:0));
        const double av=alpha(x,y), bv=alpha(x+(vertical?0:1),y+(vertical?1:0));
        if (av == bv) invalid(ex,"contour crossing requires unlike samples");
        const double t=std::clamp((level-av)/(bv-av),1.0/1024,1023.0/1024);
        const u64 key=vertical ? (u64{1}<<60)+static_cast<u64>(y)*(width+2)+x :
            static_cast<u64>(y)*(width+1)+x;
        return {key,{a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t}};
    }
    Vec2 mapped(Point p) const {
        const double w=width, h=height;
        Point q=p;
        switch (source.mapping.transform.kind()) {
        case 0: break;
        case 1: q={p.y,w-p.x}; break;
        case 2: q={w-p.x,h-p.y}; break;
        case 3: q={h-p.y,p.x}; break;
        case 4: q={w-p.x,p.y}; break;
        case 5: q={p.x,h-p.y}; break;
        case 6: q={p.y,p.x}; break;
        default: q={h-p.y,w-p.x}; break;
        }
        const auto scale=source.mapping.transform.mapExtent(Vec2{source.buffer->raster.scaleX,source.buffer->raster.scaleY});
        Vec2 out{static_cast<f32>((q.x+source.mapping.origin.x)/scale.x),
                 static_cast<f32>((q.y+source.mapping.origin.y)/scale.y)};
        if (!std::isfinite(out.x) || !std::isfinite(out.y)) invalid(ex,"contour coordinates are outside f32 range");
        if (out.x == 0) out.x=0;
        if (out.y == 0) out.y=0;
        return out;
    }
    void segment(Endpoint a, Endpoint b) {
        if (segments.size() == kMaxGeometryVertices) invalid(ex,"contour exceeds 65536 extracted segments");
        ex.spend(2);
        segments.push_back({a.key,b.key,mapped(a.p),0,static_cast<u32>(segments.size())});
    }
    void boundary(u32 x, u32 y, bool vertical, bool reverse) {
        const auto nx=x+(vertical?0:1), ny=y+(vertical?1:0);
        const bool a=inside(alpha(x,y)), b=inside(alpha(nx,ny));
        if (!a && !b) return;
        auto first=a?node(x,y):crossing(x,y,vertical);
        auto second=b?node(nx,ny):crossing(x,y,vertical);
        if (reverse) std::swap(first,second);
        segment(first,second);
    }
    void scan() {
        // Charge all visits before reading the raster, including empty cells.
        ex.spend(4*static_cast<u64>(width+1)*(height+1)+2*(static_cast<u64>(width)+height+2));
        // Entries are directed edge pairs T=0,R=1,B=2,L=3; -1 terminates.
        constexpr std::array<std::array<int,4>,16> table{{
            {{-1,-1,-1,-1}},{{0,3,-1,-1}},{{1,0,-1,-1}},{{1,3,-1,-1}},
            {{2,1,-1,-1}},{{0,3,2,1}},{{2,0,-1,-1}},{{2,3,-1,-1}},
            {{3,2,-1,-1}},{{0,2,-1,-1}},{{1,0,3,2}},{{1,2,-1,-1}},
            {{3,1,-1,-1}},{{0,1,-1,-1}},{{3,0,-1,-1}},{{-1,-1,-1,-1}}
        }};
        for (u32 y=0; y<=height; ++y) { ex.work(); for (u32 x=0; x<=width; ++x) {
            ex.work();
            const unsigned bits=(inside(alpha(x,y))?1u:0u)|(inside(alpha(x+1,y))?2u:0u)|
                (inside(alpha(x+1,y+1))?4u:0u)|(inside(alpha(x,y+1))?8u:0u);
            auto pairs=table[bits];
            if (options.connectivity == 1 && bits == 5) pairs={{0,1,2,3}};
            if (options.connectivity == 1 && bits == 10) pairs={{3,0,1,2}};
            const auto endpoint=[&](int edge) {
                switch (edge) {
                case 0: return crossing(x,y,false);
                case 1: return crossing(x+1,y,true);
                case 2: return crossing(x,y+1,false);
                default: return crossing(x,y,true);
                }
            };
            for (unsigned i=0; i<4 && pairs[i]>=0; i+=2) { ex.work(); segment(endpoint(pairs[i]),endpoint(pairs[i+1])); }
            if (y == 0) boundary(x,y,false,false);
            if (x == width) boundary(x+1,y,true,false);
            if (y == height) boundary(x,y+1,false,true);
            if (x == 0) boundary(x,y,true,true);
        } }
    }
};

struct Topology {
    Execution& ex;
    Buffer<Vertex,32> vertices;
    Buffer<Ring,96> rings;
    explicit Topology(Execution& e) : ex(e), vertices(ResourceAllocator<Vertex,32>(e.resources)),
        rings(ResourceAllocator<Ring,96>(e.resources)) {}
    void remove(u32 i) {
        auto& v=vertices[i]; auto& r=rings[v.ring];
        vertices[v.previous].next=v.next; vertices[v.next].previous=v.previous;
        v.alive=false; --r.count;
        if (r.start == i) r.start=v.next;
    }
    void metrics(Ring& r) {
        auto i=r.start;
        r.lo=r.hi=vertices[i].p;
        for (u32 k=0; k<r.count; ++k) {
            ex.work();
            ex.spend(1);
            const auto a=vertices[i].p;
            r.lo={std::min(r.lo.x,a.x),std::min(r.lo.y,a.y)};
            r.hi={std::max(r.hi.x,a.x),std::max(r.hi.y,a.y)};
            if (std::pair(a.y,a.x) < std::pair(vertices[r.start].p.y,vertices[r.start].p.x)) r.start=i;
            i=vertices[i].next;
        }
        double twice=0; i=r.start; const auto reference=vertices[i].p;
        for (u32 k=0; k<r.count; ++k) {
            ex.work();
            ex.spend(1);
            const auto a=vertices[i].p, b=vertices[vertices[i].next].p;
            twice+=(static_cast<double>(a.x)-reference.x)*(static_cast<double>(b.y)-reference.y)-
                   (static_cast<double>(b.x)-reference.x)*(static_cast<double>(a.y)-reference.y);
            i=vertices[i].next;
        }
        r.area=twice*0.5;
        if (!std::isfinite(r.area) || r.area == 0) invalid(ex,"contour ring has zero or nonfinite f32 area");
    }
    void trace(Buffer<Segment,40>& segments) {
        sorted(ex,segments,[](const auto& a,const auto& b){return a.first<b.first;});
        Buffer<u8,1> incoming(segments.size(),u8{0},ResourceAllocator<u8,1>(ex.resources));
        for (u32 i=0; i<segments.size(); ++i) {
            ex.work();
            ex.spend(2+std::bit_width(segments.size()));
            if (i && segments[i-1].first == segments[i].first) invalid(ex,"contour has multiple outgoing segments");
            const auto next=std::lower_bound(segments.begin(),segments.end(),segments[i].second,
                [](const Segment& s,u64 key){return s.first<key;});
            if (next == segments.end() || next->first != segments[i].second) invalid(ex,"contour is not closed");
            segments[i].next=static_cast<u32>(next-segments.begin());
            if (incoming[segments[i].next]++) invalid(ex,"contour has multiple incoming segments");
        }
        constexpr u32 visited=0x80000000u;
        for (u32 first=0; first<segments.size(); ++first) {
            ex.work();
            if (segments[first].next&visited) continue;
            Ring r; r.begin=r.start=static_cast<u32>(vertices.size()); r.discovery=segments[first].discovery;
            const auto ring=static_cast<u32>(rings.size());
            auto i=first;
            do {
                ex.work();
                ex.spend(1);
                auto& s=segments[i];
                if (s.next&visited) invalid(ex,"contour ring is not a simple cycle");
                vertices.push_back({s.point,0,0,ring,true});
                r.discovery=std::min(r.discovery,s.discovery);
                const auto next=s.next; s.next|=visited; i=next; ++r.rawCount;
            } while (i != first);
            if (r.rawCount<3) invalid(ex,"contour ring requires three distinct vertices");
            r.count=r.rawCount; rings.push_back(r);
            for (u32 k=0; k<r.rawCount; ++k) {
                ex.work();
                auto& v=vertices[r.begin+k];
                v.previous=r.begin+(k+r.rawCount-1)%r.rawCount;
                v.next=r.begin+(k+1)%r.rawCount;
            }
            // Remove exactly forward collinear points before the public ring cap.
            for (u32 k=0; k<r.rawCount; ++k) {
                ex.work();
                ex.spend(1); const auto id=r.begin+k; const auto v=vertices[id];
                const auto a=vertices[v.previous].p, b=v.p, c=vertices[v.next].p;
                if (same(a,b) || same(b,c)) invalid(ex,"contour edge collapses in f32 coordinates");
                if (turn(a,b,c)==0 && onSegment(a,c,b) && rings.back().count>3) remove(id);
            }
            if (rings.back().count>kMaxRingVertices) invalid(ex,"contour ring exceeds 2048 vertices");
            metrics(rings.back());
        }
    }
    bool contains(const Ring& r,Vec2 p) {
        bool inside=false; auto i=r.start;
        for (u32 k=0; k<r.count; ++k) {
            ex.work();
            ex.spend(1);
            const auto a=vertices[i].p, b=vertices[vertices[i].next].p;
            if ((a.y>p.y)!=(b.y>p.y)) {
                const double x=a.x+(static_cast<double>(p.y)-a.y)*(static_cast<double>(b.x)-a.x)/(static_cast<double>(b.y)-a.y);
                if (p.x<x) inside=!inside;
            }
            i=vertices[i].next;
        }
        return inside;
    }
    void validate() {
        Buffer<u32,4> edges{ResourceAllocator<u32,4>(ex.resources)};
        for (u32 i=0; i<vertices.size(); ++i) { ex.work(); if (vertices[i].alive) {
            ex.spend(1);
            const auto v=vertices[i];
            if (turn(vertices[v.previous].p,v.p,vertices[v.next].p)==0 &&
                !onSegment(vertices[v.previous].p,vertices[v.next].p,v.p)) invalid(ex,"contour edge backtracks");
            edges.push_back(i);
        } }
        const auto minX=[&](u32 i){return std::min(vertices[i].p.x,vertices[vertices[i].next].p.x);};
        sorted(ex,edges,[&](u32 a,u32 b){return std::pair(minX(a),a)<std::pair(minX(b),b);});
        for (u32 at=0; at<edges.size(); ++at) {
            ex.work();
            const auto i=edges[at]; const auto a=vertices[i].p, b=vertices[vertices[i].next].p;
            for (u32 bt=at+1; bt<edges.size(); ++bt) {
                ex.work();
                ex.spend(1); const auto j=edges[bt];
                if (minX(j)>std::max(a.x,b.x)) break;
                if (vertices[i].next==j || vertices[j].next==i) continue;
                if (intersects(a,b,vertices[j].p,vertices[vertices[j].next].p))
                    invalid(ex,"contour rings touch or intersect in f32 coordinates");
            }
        }
    }
    void hierarchy() {
        for (u32 i=0; i<rings.size(); ++i) {
            ex.work();
            auto& r=rings[i]; double smallest=std::numeric_limits<double>::infinity();
            for (u32 j=0; j<rings.size(); ++j) {
                ex.work();
                ex.spend(1); const auto& p=rings[j];
                if (i==j || std::abs(p.area)<=std::abs(r.area) || std::abs(p.area)>=smallest ||
                    r.lo.x<p.lo.x || r.lo.y<p.lo.y || r.hi.x>p.hi.x || r.hi.y>p.hi.y) continue;
                if (contains(p,vertices[r.start].p)) { r.parent=j; smallest=std::abs(p.area); }
            }
        }
        u32 components=0;
        for (auto& r:rings) {
            ex.work();
            for (auto parent=r.parent; parent!=kNoGeometryIndex; parent=rings[parent].parent) { ex.work();  ex.spend(1); ++r.depth; }
            if (r.depth%2==0 && ++components>kMaxGeometryRegions) invalid(ex,"contour exceeds 256 unfiltered components");
            if ((r.area<0)!=(r.depth%2==1)) {
                ex.spend(r.rawCount);
                for (u32 k=0; k<r.rawCount; ++k) { ex.work(); std::swap(vertices[r.begin+k].previous,vertices[r.begin+k].next); }
                r.reversed=!r.reversed;
            }
            metrics(r);
        }
    }
    u32 originalNext(const Ring& r,u32 i) const {
        return r.begin+(i-r.begin+(r.reversed?r.rawCount-1:1))%r.rawCount;
    }
    bool nearArc(const Ring& r,u32 a,u32 b,double toleranceSquared) {
        const auto p=vertices[a].p, q=vertices[b].p;
        const double dx=static_cast<double>(q.x)-p.x, dy=static_cast<double>(q.y)-p.y, length=dx*dx+dy*dy;
        if (length==0) return false;
        for (auto i=originalNext(r,a); i!=b; i=originalNext(r,i)) {
            ex.work();
            ex.spend(1); const auto v=vertices[i].p;
            const double x=static_cast<double>(v.x)-p.x, y=static_cast<double>(v.y)-p.y;
            const double t=std::clamp((x*dx+y*dy)/length,0.0,1.0), rx=x-t*dx, ry=y-t*dy;
            if (rx*rx+ry*ry>toleranceSquared) return false;
        }
        return true;
    }
    bool chordClear(u32 a,u32 current,u32 b) {
        const auto p=vertices[a].p, q=vertices[b].p;
        for (u32 i=0; i<vertices.size(); ++i) {
            ex.work();
            ex.spend(1);
            if (!vertices[i].alive || i==a || i==current) continue;
            const auto j=vertices[i].next;
            if (i==b || j==a) {
                const auto other=vertices[i==b?j:i].p;
                if (turn(p,q,other)==0 && (onSegment(p,q,other) || onSegment(vertices[i].p,vertices[j].p,i==b?p:q))) return false;
            } else if (intersects(p,q,vertices[i].p,vertices[j].p)) return false;
        }
        return true;
    }
    bool keepsContainment(u32 ring) {
        const auto& r=rings[ring];
        for (u32 i=0; i<rings.size(); ++i) {
            ex.work();
            if (i==ring) continue;
            ex.spend(1);
            const auto p=vertices[rings[i].start].p;
            if (p.x<r.lo.x || p.x>r.hi.x || p.y<r.lo.y || p.y>r.hi.y) continue;
            bool expected=false;
            for (auto parent=rings[i].parent; parent!=kNoGeometryIndex; parent=rings[parent].parent) {
                ex.work();
                ex.spend(1); if (parent==ring) { expected=true; break; }
            }
            if (contains(r,p)!=expected) return false;
        }
        return true;
    }
    void simplify(f32 tolerance) {
        if (tolerance==0) return;
        const double squared=static_cast<double>(tolerance)*tolerance;
        for (u32 ri=0; ri<rings.size(); ++ri) {
            ex.work();
            auto& r=rings[ri]; auto i=r.start; const auto count=r.count;
            for (u32 k=0; k<count; ++k) {
                ex.work();
                ex.spend(1); const auto v=vertices[i]; const auto next=v.next;
                const double area=r.area-turn(vertices[v.previous].p,v.p,vertices[v.next].p)*0.5;
                if (r.count>3 && (area>0)==(r.area>0) && area!=0 && nearArc(r,v.previous,v.next,squared) && chordClear(v.previous,i,v.next)) {
                    const auto oldStart=r.start; remove(i);
                    if (keepsContainment(ri)) r.area=area;
                    else {
                        vertices[i].alive=true; ++r.count; r.start=oldStart;
                        vertices[v.previous].next=i; vertices[v.next].previous=i;
                    }
                }
                i=next;
            }
            metrics(r);
        }
    }
    auto key(u32 i,double area) const {
        const auto& r=rings[i]; return std::tuple(-area,r.lo.y,r.lo.x,r.hi.y,r.hi.x,r.discovery);
    }
    std::shared_ptr<const GeometryData> publish(f32 minimum) {
        ex.spend(4*rings.size()+vertices.size());
        Buffer<u32,4> components{ResourceAllocator<u32,4>(ex.resources)}, order{ResourceAllocator<u32,4>(ex.resources)};
        for (auto& r:rings) { ex.work(); r.netArea=r.area; }
        for (const auto& r:rings) { ex.work(); if (r.depth%2==1) rings[r.parent].netArea+=r.area; }
        for (u32 i=0; i<rings.size(); ++i) { ex.work(); if (rings[i].depth%2==0) {
            if (!(rings[i].netArea>0)) invalid(ex,"contour component must have positive area after holes");
            if (rings[i].netArea>=minimum) components.push_back(i);
        } }
        sorted(ex,components,[&](u32 a,u32 b){return key(a,rings[a].netArea)<key(b,rings[b].netArea);});
        for (u32 i=0; i<components.size(); ++i) { ex.work();  auto& r=rings[components[i]]; r.keep=true; r.sourceId=static_cast<i32>(i); }
        for (auto& r:rings) { ex.work(); if (r.depth%2==1) { r.keep=rings[r.parent].keep; r.sourceId=rings[r.parent].sourceId; } }
        for (u32 i=0; i<rings.size(); ++i) { ex.work(); if (rings[i].keep) order.push_back(i); }
        sorted(ex,order,[&](u32 a,u32 b) {
            return std::pair(rings[a].depth,key(a,std::abs(rings[a].area)))<
                   std::pair(rings[b].depth,key(b,std::abs(rings[b].area)));
        });
        Buffer<u32,4> mapped(rings.size(),kNoGeometryIndex,ResourceAllocator<u32,4>(ex.resources));
        Buffer<GeometryEdgeUse,8> uses{ResourceAllocator<GeometryEdgeUse,8>(ex.resources)};
        Buffer<u32,4> holes{ResourceAllocator<u32,4>(ex.resources)};
        GeometryBuilder builder(ex,GeometryOrigin::Contours);
        u32 points=0, edges=0;
        for (const auto index:order) {
            ex.work();
            auto& r=rings[index]; auto i=r.start;
            for (u32 k=0; k<r.count; ++k) { ex.work();  builder.point(vertices[i].p); i=vertices[i].next; }
            uses.clear();
            for (u32 k=0; k<r.count; ++k) {
                ex.work();
                const auto a=points+k, b=points+(k+1)%r.count;
                const auto edge=builder.edge(std::min(a,b),std::max(a,b),{edges++,r.sourceId,-1,EdgeOrigin::Contour});
                uses.push_back({edge,a>b});
            }
            auto parent=r.parent;
            while (parent!=kNoGeometryIndex && !rings[parent].keep) { ex.work();  ex.spend(1); parent=rings[parent].parent; }
            mapped[index]=builder.ring(uses,parent==kNoGeometryIndex?kNoGeometryIndex:mapped[parent]);
            points+=r.count;
        }
        for (const auto component:components) {
            ex.work();
            holes.clear();
            for (const auto i:order) { ex.work();  ex.spend(1); if (rings[i].parent==component && rings[i].depth%2==1) holes.push_back(mapped[i]); }
            builder.region(mapped[component],rings[component].sourceId,holes);
        }
        return std::move(builder).finish();
    }
};
} // namespace

std::shared_ptr<const GeometryData> maskContours(Execution& ex,const MaskValue& source,const ContourOptions& options) {
    if (!std::isfinite(options.threshold) || options.threshold<0 || options.threshold>1) invalid(ex,"contour threshold must be finite in 0..1");
    if (!std::isfinite(options.simplify) || options.simplify<0) invalid(ex,"contour simplify must be finite and nonnegative");
    if (!std::isfinite(options.minArea) || options.minArea<0) invalid(ex,"contour min_area must be finite and nonnegative");
    if (options.connectivity<0 || options.connectivity>1) invalid(ex,"contour connectivity must be four or eight");
    if (!source.buffer) ex.trap("mask is unassigned",ErrorReason::RuntimeViolation);
    validateSurfaceView(ex,*source.buffer,source.mapping,false);
    if (source.buffer->raster.width>kMaxCanvasSide || source.buffer->raster.height>kMaxCanvasSide) invalid(ex,"contour raster extent exceeds limit");
    ex.resources.resourceLifetime()->location=ex.activeInstruction;
    const ResourceReservation workspace(ex.resources,4096);
    Extractor extractor(ex,source,options); extractor.scan();
    Topology topology(ex); topology.trace(extractor.segments); topology.validate(); topology.hierarchy();
    topology.simplify(options.simplify);
    return topology.publish(options.minArea);
}

bool stepContours(Execution& ex,Instruction ins) {
    if (ins.opcode!=Opcode::Contour && ins.opcode!=Opcode::ContourConfig) return false;
    ContourOptions options;
    if (ins.opcode==Opcode::ContourConfig) { options.connectivity=ex.popI32(); options.minArea=ex.popF32(); options.simplify=ex.popF32(); }
    options.threshold=ex.popF32(); const auto source=ex.popMask(false);
    ex.push(Contours2dValue{maskContours(ex,source,options)});
    ++ex.frame().ip;
    return true;
}
} // namespace iprop::detail
