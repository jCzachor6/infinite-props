#include "iprop/Voronoi.hpp"
#include "iprop/VmInternals.hpp"

#include <array>
#include <bit>
#include <cmath>
#include <limits>
#include <tuple>

namespace iprop::detail {
namespace {
struct Point { double x = 0, y = 0; };
struct Line {
    Point n, middle;
    double value(Point p) const { return n.x*(p.x-middle.x)+n.y*(p.y-middle.y); }
    double classified(Point p) const {
        const double d = value(p);
        const double error = 64*std::numeric_limits<double>::epsilon()*
            (std::abs(n.x)*(std::abs(p.x)+std::abs(middle.x))+
             std::abs(n.y)*(std::abs(p.y)+std::abs(middle.y)));
        return std::abs(d) <= error ? 0 : d;
    }
};
struct Vertex { Point p; u32 incoming = 0; };
struct Range { u32 begin = 0, count = 0; };
struct Occurrence { Vec2 p; u32 point = 0, edge = 0; };
struct EdgeUse { u32 first = 0, second = 0, occurrence = 0, cell = 0; bool reversed = false; };
struct Workspace {
    std::array<Vertex,kMaxGeometrySites+8> a{},b{};
    std::array<Vec2,kMaxGeometrySites> sites{};
    std::array<Point,kMaxGeometrySites> local{};
    std::array<Range,kMaxGeometrySites> cells{};
    std::array<u64,kMaxGeometrySites*4> adjacent{};
    std::array<GeometryEdgeUse,kMaxGeometrySites+8> uses{};
    std::array<i32,kMaxGeometrySites> neighbors{};
};
[[noreturn]] void invalid(Execution& ex,const char* text) { ex.trap(text,ErrorReason::NumericRange); }
bool finite(Vec2 p) { return std::isfinite(p.x) && std::isfinite(p.y); }
bool same(Vec2 a,Vec2 b) { return a.x == b.x && a.y == b.y; }
double turn(Vec2 a,Vec2 b,Vec2 c) {
    return (static_cast<double>(b.x)-a.x)*(static_cast<double>(c.y)-a.y)-
           (static_cast<double>(b.y)-a.y)*(static_cast<double>(c.x)-a.x);
}
bool onSegment(Vec2 a,Vec2 b,Vec2 p) {
    return p.x>=std::min(a.x,b.x) && p.x<=std::max(a.x,b.x) &&
        p.y>=std::min(a.y,b.y) && p.y<=std::max(a.y,b.y);
}
bool intersects(Vec2 a,Vec2 b,Vec2 c,Vec2 d) {
    const double abC=turn(a,b,c),abD=turn(a,b,d),cdA=turn(c,d,a),cdB=turn(c,d,b);
    if ((abC==0 && onSegment(a,b,c)) || (abD==0 && onSegment(a,b,d)) ||
        (cdA==0 && onSegment(c,d,a)) || (cdB==0 && onSegment(c,d,b))) return true;
    return ((abC>0 && abD<0) || (abC<0 && abD>0)) && ((cdA>0 && cdB<0) || (cdA<0 && cdB>0));
}
struct Rectangle {
    Vec2 origin,end;
    double scale;
    Point size;
    Rectangle(Execution& ex,Vec2 o,Vec2 s) : origin(o),end{o.x+s.x,o.y+s.y},scale(0) {
        if (!finite(o) || !finite(s) || !finite(end) || !(s.x > 0 && s.y > 0) ||
            !(end.x > o.x && end.y > o.y))
            invalid(ex,"voronoi rectangle must be finite, positive and representable");
        const double w = static_cast<double>(end.x)-o.x, h = static_cast<double>(end.y)-o.y;
        scale = std::max(w,h); size = {w/scale,h/scale};
    }
    Point local(Vec2 p) const { return {(static_cast<double>(p.x)-origin.x)/scale,(static_cast<double>(p.y)-origin.y)/scale}; }
    Vec2 world(Point p) const {
        Vec2 out{static_cast<f32>(origin.x+std::clamp(p.x,0.0,size.x)*scale),
                 static_cast<f32>(origin.y+std::clamp(p.y,0.0,size.y)*scale)};
        if (out.x == 0) out.x = 0; // canonical positive zero
        if (out.y == 0) out.y = 0;
        return out;
    }
    bool contains(Vec2 p) const { return finite(p) && p.x >= origin.x && p.x <= end.x && p.y >= origin.y && p.y <= end.y; }
};
void checkRelax(Execution& ex,i32 relax) {
    if (relax < 0 || relax > 4) invalid(ex,"voronoi relax must be in 0..4");
}
void checkSites(Execution& ex,const Rectangle& rect,const Workspace& ws,u32 count) {
    ex.spend(1+static_cast<u64>(count)*(count+1)/2);
    for (u32 i=0;i<count;++i) {
        ex.work();
        if (!rect.contains(ws.sites[i])) invalid(ex,"voronoi site must be finite and inside the rectangle");
        for (u32 j=0;j<i;++j) { ex.work(); if (same(ws.sites[i],ws.sites[j])) invalid(ex,"voronoi sites must be distinct"); }
    }
}
struct Metric {
    double xx = 1, xy = 0, yy = 1;
    explicit Metric(const VoronoiOptions& o) {
        if (o.anisotropy == 1) return; // exact neutral path, independent of direction
        const double x=o.direction.x,y=o.direction.y,length=std::sqrt(x*x+y*y);
        const double ux=x/length,uy=y/length,k=1/(static_cast<double>(o.anisotropy)*o.anisotropy);
        xx=k*ux*ux+uy*uy; xy=(k-1)*ux*uy; yy=k*uy*uy+ux*ux;
    }
};
struct Planes {
    const Rectangle& rect;
    const Workspace& ws;
    u32 count;
    Metric metric;
    u32 pair(u32 a,u32 b) const { return 4+std::min(a,b)*count+std::max(a,b); }
    Line line(u32 id) const {
        switch (id) {
        case 0: return {{0,-1},{0,0}};
        case 1: return {{1,0},{rect.size.x,0}};
        case 2: return {{0,1},{0,rect.size.y}};
        case 3: return {{-1,0},{0,0}};
        default: break;
        }
        const auto a=ws.local[(id-4)/count],b=ws.local[(id-4)%count];
        const double dx=b.x-a.x,dy=b.y-a.y;
        Point n{metric.xx*dx+metric.xy*dy,metric.xy*dx+metric.yy*dy};
        const double scale=std::max(std::abs(n.x),std::abs(n.y));
        n.x/=scale; n.y/=scale;
        return {n,{a.x+(b.x-a.x)*0.5,a.y+(b.y-a.y)*0.5}};
    }
    Point intersection(Execution& ex,u32 a,u32 b) const {
        if (a > b) std::swap(a,b);
        const auto p=line(a),q=line(b);
        const double denominator=q.n.y*p.n.x-q.n.x*p.n.y;
        if (denominator == 0) invalid(ex,"voronoi intersection is not representable");
        const double t=-q.value(p.middle)/denominator;
        Point out{p.middle.x-t*p.n.y,p.middle.y+t*p.n.x};
        // A boundary coordinate is exact even after a nearly parallel solve.
        for (const auto id : {a,b}) {
            ex.work();
            if (id == 0) out.y=0;
            if (id == 1) out.x=rect.size.x;
            if (id == 2) out.y=rect.size.y;
            if (id == 3) out.x=0;
        }
        if (!std::isfinite(out.x) || !std::isfinite(out.y)) invalid(ex,"voronoi intersection is not representable");
        return out;
    }
    Vec2 canonical(Execution& ex,Point p,u32 incoming,u32 outgoing,u32 cell) const {
        ex.spend(count+5);
        u32 boundaries=0;
        for (u32 k=0;k<4;++k) { ex.work(); if (line(k).classified(p) == 0) boundaries|=1u<<k; }
        for (const auto id : {incoming,outgoing}) { ex.work(); if (id < 4) boundaries|=1u<<id; }
        std::array<u32,3> tied{count,count,count};
        const auto forced=[&](u32 id,u32 candidate) {
            return id >= 4 && ((id-4)/count == candidate || (id-4)%count == candidate);
        };
        for (u32 j=0;j<count;++j) {
            ex.work();
            if (j != cell && !forced(incoming,j) && !forced(outgoing,j) && line(pair(cell,j)).classified(p) != 0) continue;
            if (tied[0] == count) tied[0]=j;
            else if (tied[1] == count) tied[1]=j;
            else if (tied[2] == count) tied[2]=j;
        }
        u32 first=4,second=4;
        for (u32 k=0;k<4;++k) { ex.work(); if (boundaries & (1u<<k)) { if (first == 4) first=k; else second=k; } }
        if (second != 4) p=intersection(ex,first,second);
        else if (first != 4 && tied[1] != count) p=intersection(ex,first,pair(tied[0],tied[1]));
        else if (first == 4 && tied[2] != count) p=intersection(ex,pair(tied[0],tied[1]),pair(tied[0],tied[2]));
        else invalid(ex,"voronoi vertex has incomplete support");
        const double tolerance=256*std::numeric_limits<double>::epsilon();
        if (p.x < -tolerance*rect.size.x || p.x > (1+tolerance)*rect.size.x ||
            p.y < -tolerance*rect.size.y || p.y > (1+tolerance)*rect.size.y)
            invalid(ex,"voronoi vertex is outside the rectangle");
        return rect.world(p);
    }
};
// Iterative heapsort: no uncharged recursive/STL scratch and a fixed bound.
template<class Iterator,class Less> void sort(Execution& ex,Iterator begin,Iterator end,Less less) {
    const auto size=static_cast<std::size_t>(end-begin);
    ex.spend(1+4*static_cast<u64>(size)*(1+std::bit_width(size)));
    const auto sift=[&](std::size_t root,std::size_t count) {
        while (root < count/2) {
            ex.work();
            auto child=root*2+1;
            if (child+1 < count && less(begin[child],begin[child+1])) ++child;
            if (!less(begin[root],begin[child])) break;
            std::swap(begin[root],begin[child]); root=child;
        }
    };
    for (auto root=size/2;root>0;--root) { ex.work(); sift(root-1,size); }
    for (auto n=size;n>1;--n) { ex.work();  std::swap(begin[0],begin[n-1]); sift(0,n-1); }
}
std::shared_ptr<const GeometryData> construct(Execution& ex,const Rectangle& rect,Workspace& ws,u32 count,const VoronoiOptions& options) {
    for (u32 i=0;i<count;++i) { ex.work(); ws.local[i]=rect.local(ws.sites[i]); }
    const Planes planes{rect,ws,count,Metric(options)};
    ResourceVector<Occurrence,16> vertices{ResourceAllocator<Occurrence,16>(ex.resources)};
    ws.adjacent.fill(0);
    for (u32 cell=0;cell<count;++cell) {
        ex.work();
        auto* input=&ws.a; auto* output=&ws.b; u32 length=4;
        (*input)[0]={{0,0},3}; (*input)[1]={{rect.size.x,0},0};
        (*input)[2]={rect.size,1}; (*input)[3]={{0,rect.size.y},2};
        for (u32 other=0;other<count;++other) {
            ex.work();
            if (other == cell) continue;
            ex.spend(1+2*length);
            const auto id=planes.pair(cell,other); const auto line=planes.line(id);
            const double sign=cell < other ? 1 : -1;
            u32 next=0;
            const auto append=[&](Vertex v) {
                if (next && (*output)[next-1].p.x == v.p.x && (*output)[next-1].p.y == v.p.y) return;
                if (next >= output->size()) invalid(ex,"voronoi cell exceeds vertex limit");
                (*output)[next++]=v;
            };
            for (u32 k=0;k<length;++k) {
                ex.work();
                const auto a=(*input)[k],b=(*input)[(k+1)%length];
                const bool insideA=sign*line.classified(a.p) <= 0,insideB=sign*line.classified(b.p) <= 0;
                if (insideA != insideB) append({planes.intersection(ex,b.incoming,id),insideA ? b.incoming : id});
                if (insideB) append(b);
            }
            if (next > 1 && (*output)[0].p.x == (*output)[next-1].p.x && (*output)[0].p.y == (*output)[next-1].p.y) {
                (*output)[0].incoming=(*output)[next-1].incoming; --next;
            }
            if (next < 3) invalid(ex,"voronoi cell cannot be represented");
            length=next; std::swap(input,output);
        }
        auto& range=ws.cells[cell]; range.begin=static_cast<u32>(vertices.size());
        for (u32 k=0;k<length;++k) {
            ex.work();
            const auto p=planes.canonical(ex,(*input)[k].p,(*input)[k].incoming,(*input)[(k+1)%length].incoming,cell);
            if (vertices.size() > range.begin && same(vertices.back().p,p)) continue;
            if (vertices.size() == kMaxGeometryVertices) invalid(ex,"voronoi exceeds 65536 ring vertices");
            vertices.push_back({p,0,0});
        }
        if (vertices.size() > range.begin+1 && same(vertices[range.begin].p,vertices.back().p)) vertices.resize(vertices.size()-1);
        range.count=static_cast<u32>(vertices.size())-range.begin;
        if (range.count < 3) invalid(ex,"voronoi cell cannot be represented in f32");
    }
    ResourceVector<u32,4> order{ResourceAllocator<u32,4>(ex.resources)};
    order.reserve(vertices.size());
    for (u32 i=0;i<vertices.size();++i) { ex.work(); order.push_back(i); }
    sort(ex,order.begin(),order.end(),[&](u32 a,u32 b) {
        return std::tie(vertices[a].p.x,vertices[a].p.y,a) < std::tie(vertices[b].p.x,vertices[b].p.y,b);
    });
    GeometryBuilder builder(ex,GeometryOrigin::Diagram);
    u32 point=0;
    for (std::size_t k=0;k<order.size();++k) {
        ex.work();
        const auto i=order[k];
        if (!k || !same(vertices[i].p,vertices[order[k-1]].p)) point=builder.point(vertices[i].p);
        vertices[i].point=point;
    }
    ResourceVector<EdgeUse,24> edges{ResourceAllocator<EdgeUse,24>(ex.resources)};
    edges.reserve(vertices.size());
    for (u32 cell=0;cell<count;++cell) {
        ex.work();
        const auto range=ws.cells[cell];
        ex.spend(static_cast<u64>(range.count)*range.count);
        for (u32 k=0;k<range.count;++k) {
            ex.work();
            const auto i=range.begin+k,j=range.begin+(k+1)%range.count;
            const auto p=vertices[i].p,q=vertices[j].p,r=vertices[range.begin+(k+2)%range.count].p;
            if (turn(p,q,r)==0 && (onSegment(p,q,r) || onSegment(q,r,p)))
                invalid(ex,"voronoi cell has a collapsed f32 edge");
            for (u32 other=k+2;other<range.count;++other) {
                ex.work();
                if (k==0 && other==range.count-1) continue;
                const auto c=vertices[range.begin+other].p,d=vertices[range.begin+(other+1)%range.count].p;
                if (intersects(p,q,c,d)) invalid(ex,"voronoi cell cannot be represented as simple f32 geometry");
            }
            const auto a=vertices[i].point,b=vertices[j].point;
            edges.push_back({std::min(a,b),std::max(a,b),i,cell,a>b});
        }
    }
    sort(ex,edges.begin(),edges.end(),[](const EdgeUse& a,const EdgeUse& b) {
        return std::tie(a.first,a.second,a.cell) < std::tie(b.first,b.second,b.cell);
    });
    u32 canonicalEdge=0;
    std::array<f32,4> boundaryEnd{rect.origin.x,rect.origin.y,rect.origin.x,rect.origin.y};
    for (std::size_t i=0;i<edges.size();) {
        ex.work();
        auto end=i+1;
        while (end<edges.size() && edges[i].first == edges[end].first && edges[i].second == edges[end].second) { ex.work(); ++end; }
        const auto& a=edges[i];
        EdgeProvenance provenance{canonicalEdge++,static_cast<i32>(a.cell),-1,EdgeOrigin::Boundary};
        if (end-i == 2) {
            const auto& b=edges[i+1];
            if (a.cell == b.cell || a.reversed == b.reversed) invalid(ex,"voronoi shared edge is inconsistent");
            provenance.neighbor=static_cast<i32>(b.cell); provenance.origin=EdgeOrigin::Diagram;
            ws.adjacent[a.cell*4+b.cell/64]|=u64{1}<<(b.cell%64);
            ws.adjacent[b.cell*4+a.cell/64]|=u64{1}<<(a.cell%64);
        } else if (end-i == 1) {
            const auto range=ws.cells[a.cell]; const auto k=a.occurrence-range.begin;
            const auto p=vertices[a.occurrence].p,q=vertices[range.begin+(k+1)%range.count].p;
            u32 side=4;f32 start=0,finish=0;
            if (p.y==q.y && p.y==rect.origin.y) { side=0;start=std::min(p.x,q.x);finish=std::max(p.x,q.x); }
            else if (p.x==q.x && p.x==rect.end.x) { side=1;start=std::min(p.y,q.y);finish=std::max(p.y,q.y); }
            else if (p.y==q.y && p.y==rect.end.y) { side=2;start=std::min(p.x,q.x);finish=std::max(p.x,q.x); }
            else if (p.x==q.x && p.x==rect.origin.x) { side=3;start=std::min(p.y,q.y);finish=std::max(p.y,q.y); }
            if (side==4) invalid(ex,"voronoi shared edge cannot be represented in f32");
            if (start!=boundaryEnd[side]) invalid(ex,"voronoi rectangle boundary has a gap or overlap");
            boundaryEnd[side]=finish;
        } else invalid(ex,"voronoi edge has more than two owners");
        const auto id=builder.edge(a.first,a.second,provenance);
        for (auto j=i;j<end;++j) { ex.work(); vertices[edges[j].occurrence].edge=id; }
        i=end;
    }
    if (boundaryEnd!=std::array<f32,4>{rect.end.x,rect.end.y,rect.end.x,rect.end.y})
        invalid(ex,"voronoi rectangle boundary is incomplete");
    for (u32 cell=0;cell<count;++cell) {
        ex.work();
        builder.site(ws.sites[cell]);
        const auto range=ws.cells[cell];
        for (u32 k=0;k<range.count;++k) {
            ex.work();
            const auto& a=vertices[range.begin+k]; const auto& b=vertices[range.begin+(k+1)%range.count];
            ws.uses[k]={a.edge,a.point>b.point};
        }
        u32 neighbors=0;
        ex.spend(count);
        for (u32 j=0;j<count;++j) { ex.work(); if (ws.adjacent[cell*4+j/64] & (u64{1}<<(j%64))) ws.neighbors[neighbors++]=static_cast<i32>(j); }
        const auto ring=builder.ring(std::span(ws.uses.data(),range.count));
        builder.region(ring,static_cast<i32>(cell),{},std::span(ws.neighbors.data(),neighbors));
    }
    return std::move(builder).finish();
}
u32 hash(u32 x) {
    x^=x>>16; x*=0x7feb352du; x^=x>>15; x*=0x846ca68bu; return x^(x>>16);
}
double unit(u32 seed,u32 index,u32 lane,u32 attempt=0) {
    // Counter based; neither Worley coordinates nor the active RNG stream.
    const auto bits=hash(seed^0x564f524fu^hash(index+0x9e3779b9u)^hash(lane+0x85ebca6bu)^hash(attempt+0xc2b2ae35u));
    return (static_cast<double>(bits>>8)+0.5)/16777216.0;
}
u32 columns(u32 count,double width,double height) {
    const double wanted=std::sqrt(static_cast<double>(count)*width/height);
    return wanted >= count ? count : std::max(1u,static_cast<u32>(std::ceil(wanted)));
}
Point rowPoint(const Rectangle& rect,const VoronoiOptions& o,u32 index,u32 count) {
    const double dx=o.direction.x,dy=o.direction.y,length=std::sqrt(dx*dx+dy*dy);
    const Point u{dx/length,dy/length},v{-u.y,u.x};
    const double minU=std::min(0.0,u.x*rect.size.x)+std::min(0.0,u.y*rect.size.y);
    const double maxU=std::max(0.0,u.x*rect.size.x)+std::max(0.0,u.y*rect.size.y);
    const double minV=std::min(0.0,v.x*rect.size.x)+std::min(0.0,v.y*rect.size.y);
    const double maxV=std::max(0.0,v.x*rect.size.x)+std::max(0.0,v.y*rect.size.y);
    const auto cols=columns(count,maxU-minU,maxV-minV),rows=(count+cols-1)/cols,row=index/cols,col=index%cols;
    const auto inRow=std::min(cols,count-row*cols);
    const double t=minV+(maxV-minV)*(row+0.5+o.rowJitter*(unit(static_cast<u32>(o.seed),row,2)-0.5))/rows;
    double lo=-std::numeric_limits<double>::infinity(),hi=std::numeric_limits<double>::infinity();
    const auto slab=[&](double direction,double base,double size) {
        if (direction == 0) return;
        double a=-base/direction,b=(size-base)/direction;
        if (a>b) std::swap(a,b);
        lo=std::max(lo,a); hi=std::min(hi,b);
    };
    slab(u.x,v.x*t,rect.size.x); slab(u.y,v.y*t,rect.size.y);
    double phase=(col+0.5+(row%2 ? o.rowOffset : 0))/inRow;
    phase-=std::floor(phase);
    const double s=lo+(hi-lo)*phase;
    return {u.x*s+v.x*t,u.y*s+v.y*t};
}
std::shared_ptr<const GeometryData> relax(Execution& ex,const Rectangle& rect,Workspace& ws,u32 count,const VoronoiOptions& o) {
    checkSites(ex,rect,ws,count);
    for (i32 iteration=0;;++iteration) {
        ex.work();
        auto result=construct(ex,rect,ws,count,o);
        if (iteration == o.relax) return result;
        ex.spend(count);
        for (u32 i=0;i<count;++i) { ex.work(); ws.sites[i]=result->regions[i].center; }
        result.reset();
        checkSites(ex,rect,ws,count);
    }
}
} // namespace

std::shared_ptr<const GeometryData> generatedVoronoi(Execution& ex,Vec2 origin,Vec2 size,const VoronoiOptions& o) {
    ex.resources.resourceLifetime()->location=ex.activeInstruction;
    const Rectangle rect(ex,origin,size);
    if (o.count < 1 || o.count > 256) invalid(ex,"voronoi count must be in 1..256");
    if (o.method != 0 && o.method != 1) invalid(ex,"unknown voronoi method");
    const auto unitRange=[](f32 x) { return std::isfinite(x) && x >= 0 && x <= 1; };
    if (!unitRange(o.jitter)) invalid(ex,"voronoi jitter must be finite and in 0..1");
    checkRelax(ex,o.relax);
    if (!std::isfinite(o.anisotropy) || o.anisotropy < 0.25f || o.anisotropy > 4)
        invalid(ex,"voronoi anisotropy must be finite and in 0.25..4");
    if (!finite(o.direction) || (o.direction.x == 0 && o.direction.y == 0)) invalid(ex,"voronoi direction must be finite and nonzero");
    if (!unitRange(o.rowBias) || !unitRange(o.rowJitter) || !unitRange(o.rowOffset))
        invalid(ex,"voronoi row parameters must be finite and in 0..1");
    ResourceReservation reservation(ex.resources,sizeof(Workspace));
    ex.spend(1+sizeof(Workspace)/64);
    Workspace ws;
    const auto count=static_cast<u32>(o.count),seed=static_cast<u32>(o.seed);
    const auto cols=columns(count,rect.size.x,rect.size.y),rows=(count+cols-1)/cols;
    for (u32 i=0;i<count;++i) {
        ex.work();
        bool accepted=false;
        for (u32 attempt=0;attempt<32;++attempt) {
            ex.work();
            ex.spend(16+i);
            Point p;
            if (o.method == 0 || attempt) p={unit(seed,i,0,attempt)*rect.size.x,unit(seed,i,1,attempt)*rect.size.y};
            else {
                const auto row=i/cols,col=i%cols,inRow=std::min(cols,count-row*cols);
                p={(col+0.5+o.jitter*(unit(seed,i,0)-0.5))*rect.size.x/inRow,
                   (row+0.5+o.jitter*(unit(seed,i,1)-0.5))*rect.size.y/rows};
            }
            if (o.rowBias != 0 && !attempt) {
                const auto row=rowPoint(rect,o,i,count);
                p={p.x+(row.x-p.x)*o.rowBias,p.y+(row.y-p.y)*o.rowBias};
            }
            const auto candidate=rect.world(p);
            bool duplicate=false;
            for (u32 j=0;j<i;++j) { ex.work(); duplicate|=same(ws.sites[j],candidate); }
            if (!duplicate) { ws.sites[i]=candidate; accepted=true; break; }
        }
        if (!accepted) invalid(ex,"voronoi generated sites collide after 32 attempts");
    }
    return relax(ex,rect,ws,count,o);
}
std::shared_ptr<const GeometryData> explicitVoronoi(Execution& ex,Vec2 origin,Vec2 size,std::span<const Vec2> sites,i32 iterations) {
    ex.resources.resourceLifetime()->location=ex.activeInstruction;
    const Rectangle rect(ex,origin,size);
    if (sites.empty() || sites.size() > kMaxGeometrySites) invalid(ex,"voronoi sites count must be in 1..256");
    checkRelax(ex,iterations);
    ResourceReservation reservation(ex.resources,sizeof(Workspace));
    ex.spend(1+sizeof(Workspace)/64+sites.size());
    Workspace ws;
    for (std::size_t i=0;i<sites.size();++i) { ex.work(); ws.sites[i]=sites[i]; }
    VoronoiOptions options; options.relax=iterations;
    return relax(ex,rect,ws,static_cast<u32>(sites.size()),options);
}
bool stepVoronoi(Execution& ex,Instruction ins) {
    switch (ins.opcode) {
    case Opcode::Voronoi: case Opcode::VoronoiConfig: case Opcode::VoronoiMetric: case Opcode::VoronoiRows: {
        VoronoiOptions o;
        if (ins.opcode == Opcode::VoronoiRows) { o.rowOffset=ex.popF32();o.rowJitter=ex.popF32();o.rowBias=ex.popF32(); }
        if (ins.opcode == Opcode::VoronoiRows || ins.opcode == Opcode::VoronoiMetric) { o.direction=ex.popVec2();o.anisotropy=ex.popF32(); }
        if (ins.opcode != Opcode::Voronoi) { o.relax=ex.popI32();o.jitter=ex.popF32();o.method=ex.popI32(); }
        o.seed=ex.popI32();o.count=ex.popI32();const auto size=ex.popVec2(),origin=ex.popVec2();
        ex.push(Diagram2dValue{generatedVoronoi(ex,origin,size,o)}); break;
    }
    case Opcode::VoronoiSites: case Opcode::VoronoiSitesRelax: {
        const auto iterations=ins.opcode == Opcode::VoronoiSitesRelax ? ex.popI32() : 0;
        const auto sites=ex.popHandle<ListValue>("voronoi sites");
        if (sites.type != ValueType::list(ValueType::Vec2)) ex.trap("voronoi sites must be list<vec2>",ErrorReason::StackType);
        const auto size=ex.popVec2(),origin=ex.popVec2();
        if (sites.data->elements.empty() || sites.data->elements.size() > kMaxGeometrySites)
            invalid(ex,"voronoi sites count must be in 1..256");
        ResourceReservation reservation(ex.resources,kMaxGeometrySites*8);
        std::array<Vec2,kMaxGeometrySites> points;
        ex.spend(sites.data->elements.size());
        for (std::size_t i=0;i<sites.data->elements.size();++i) {
            ex.work();
            const auto* p=std::get_if<Vec2>(&sites.data->elements[i]);
            if (!p) ex.trap("voronoi sites element must be vec2",ErrorReason::StackType);
            points[i]=*p;
        }
        ex.push(Diagram2dValue{explicitVoronoi(ex,origin,size,std::span(points.data(),sites.data->elements.size()),iterations)});
        break;
    }
    case Opcode::DiagramCount: case Opcode::DiagramSite: case Opcode::DiagramNeighbors: {
        const auto index=ins.opcode == Opcode::DiagramCount ? 0 : ex.popI32();
        const auto d=ex.popHandle<Diagram2dValue>("diagram2d");
        if (ins.opcode == Opcode::DiagramCount) { ex.push(static_cast<i32>(d.data->sites.size()));break; }
        if (index < 0 || static_cast<std::size_t>(index) >= d.data->sites.size()) ex.trap("diagram index out of range",ErrorReason::IndexRange);
        if (ins.opcode == Opcode::DiagramSite) ex.push(d.data->sites[static_cast<u32>(index)]);
        else {
            const auto neighbors=d.data->regionNeighbors(static_cast<u32>(index));
            ex.spend(neighbors.size());
            ex.allocateResource(ResourceArena::listBytes(neighbors.size()));
            auto data=std::make_shared<ListData>();data->type=ValueType::list(ValueType::I32);data->elements.reserve(neighbors.size());
            for (const auto neighbor : neighbors) { ex.work(); data->elements.emplace_back(neighbor); }
            appendWork(ex.resources.lists, data, *ex.workBudget);ex.push(ListValue{data->type,std::move(data)});
        }
        break;
    }
    default: return false;
    }
    ++ex.frame().ip;
    return true;
}
} // namespace iprop::detail
