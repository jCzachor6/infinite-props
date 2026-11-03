#include "iprop/Regions.hpp"
#include "iprop/GeometryAlgorithms.hpp"

#include <array>
#include <bit>
#include <cmath>
#include <tuple>

namespace iprop::detail {
namespace {
template<class T,u64 S> using Buffer=ResourceVector<T,S>;
constexpr u32 maxRaw=65536,maxCuts=262144;
constexpr double arcTolerance=1.0/64;
struct Point { double x,y; };
struct Segment { Vec2 a,b;EdgeProvenance provenance;u32 next=0;bool used=false; };
struct Cut { u32 segment;double t;Vec2 point; };
struct Loop { u32 begin=0,count=0,parent=kNoGeometryIndex,source=0,discovery=0;double area=0,net=0;Vec2 lo{},hi{};bool keep=false; };
struct Arc { Point a,b;u32 depth; };
struct ProfileArc {Point a,b;double first,last;u32 depth;};
struct Link {u32 edge,region;i32 source,neighbor;};
[[noreturn]] void invalid(Execution& ex,const char* message){ex.trap(message,ErrorReason::NumericRange);}
bool same(Vec2 a,Vec2 b){return a.x==b.x && a.y==b.y;}
auto key(Vec2 p){return std::pair(p.y,p.x);}
Point subtract(Vec2 a,Vec2 b){return {static_cast<double>(a.x)-b.x,static_cast<double>(a.y)-b.y};}
double cross(Point a,Point b){return a.x*b.y-a.y*b.x;}
double turn(Vec2 a,Vec2 b,Vec2 c){return cross(subtract(b,a),subtract(c,a));}
Point normal(Vec2 a,Vec2 b){const auto d=subtract(b,a);const double length=std::sqrt(d.x*d.x+d.y*d.y);return {-d.y/length,d.x/length};}
Vec2 rounded(Execution& ex,Point p){
    Vec2 q{static_cast<f32>(p.x),static_cast<f32>(p.y)};
    if(!std::isfinite(q.x)||!std::isfinite(q.y))invalid(ex,"region offset coordinates are outside f32 range");
    if(q.x==0)q.x=0;if(q.y==0)q.y=0;return q;
}
EdgeProvenance provenance(const GeometryData& d,u32 edge,i32 source){
    auto p=d.edges[edge].provenance;p.edge=d.source?p.edge:edge;
    const auto other=p.source==source?p.neighbor:p.neighbor==source?p.source:-1;
    p.source=source;p.neighbor=other;p.origin=EdgeOrigin::Offset;return p;
}
u32 gapHash(u32 x){x^=x>>16;x*=0x7feb352du;x^=x>>15;x*=0x846ca68bu;return x^(x>>16);}
// The original edge and vertex indices define one unoriented profile. Even
// repeated derivation samples that same profile on the current geometry.
struct Profile {
    Vec2 a,b;std::array<double,9> radii;
    Profile(const GeometryData& d,u32 edge,double half,double variation,i32 seed){
        const auto& root=d.source?*d.source:d;const auto original=d.source?d.edges[edge].provenance.edge:edge;
        const auto e=root.edges[original];auto first=e.first,last=e.second;
        if(key(root.points[last])<key(root.points[first]))std::swap(first,last);
        a=root.points[first];b=root.points[last];const auto s=std::bit_cast<u32>(seed);
        for(u32 j=0;j<=8;++j){
            const auto h=j==0||j==8?gapHash(s^0x9e3779b9u^gapHash((j==0?first:last)+0x68bc21ebu)):
                gapHash(s^0x85ebca6bu^gapHash(original+0x27d4eb2du)^(j*0x9e3779b9u));
            const double unit=static_cast<double>(h>>8)/16777215.0;
            radii[j]=half*((1-variation)+(2*variation)*unit);
        }
    }
    double parameter(Vec2 p)const{
        const auto v=subtract(b,a),w=subtract(p,a);return(w.x*v.x+w.y*v.y)/(v.x*v.x+v.y*v.y);
    }
    double radius(double t)const{
        const auto value=std::clamp(t,0.0,1.0)*8;const auto j=std::min(static_cast<u32>(value),7u);
        return radii[j]+(radii[j+1]-radii[j])*(value-j);
    }
};
struct Offset {
    Execution& ex;const std::shared_ptr<const GeometryData>& source;double inset;
    double variation=0;i32 seed=0;
    Buffer<Segment,48> raw,segments;
    Buffer<Cut,32> cuts;
    Buffer<Loop,80> loops;
    u32 rawVisits=0,cutVisits=0,outputVisits=0;
    Offset(Execution& e,const std::shared_ptr<const GeometryData>& s,f32 amount)
        :ex(e),source(s),inset(amount),raw(ResourceAllocator<Segment,48>(e.resources)),
         segments(ResourceAllocator<Segment,48>(e.resources)),cuts(ResourceAllocator<Cut,32>(e.resources)),
         loops(ResourceAllocator<Loop,80>(e.resources)){}
    void add(Vec2 a,Vec2 b,EdgeProvenance p){
        if(same(a,b))return;
        if(++rawVisits>maxRaw)invalid(ex,"region offset exceeds 65536 intermediate segments");
        ex.spend(1);raw.push_back({a,b,p});
    }
    void ring(u32 index,i32 id){
        const auto& d=*source;const auto& r=d.rings[index];
        Vec2 first{},last{};EdgeProvenance incoming{};bool started=false;
        const auto append=[&](Vec2 point,EdgeProvenance edge){
            if(!started){first=last=point;started=true;}else{add(last,point,incoming);last=point;}incoming=edge;
        };
        for(u32 k=0;k<r.count;++k){
            ex.spend(4);
            const auto previous=d.uses[r.begin+(k+r.count-1)%r.count],current=d.uses[r.begin+k];
            const auto a=d.start(previous),b=d.start(current),c=d.end(current);
            auto u=normal(a,b),v=normal(b,c);u={u.x*inset,u.y*inset};v={v.x*inset,v.y*inset};
            const auto p=provenance(d,current.edge,id),old=provenance(d,previous.edge,id);
            const auto join=p.edge<old.edge?p:old;
            const auto from=rounded(ex,{b.x+u.x,b.y+u.y}),to=rounded(ex,{b.x+v.x,b.y+v.y});
            append(from,join);
            const double bend=turn(a,b,c);
            if(bend*inset>0){
                // Inward corners deliberately insert negative winding lobes.
                // Positive-winding extraction removes these and over-shrunk
                // components, instead of retaining a reversed small polygon.
                append(b,join);append(to,p);
            }else if(bend*inset<0){
                const double radius=std::abs(inset);
                const Point start{u.x/radius,u.y/radius},end{v.x/radius,v.y/radius};
                std::array<Arc,64> stack;u32 depth=1;stack[0]={start,end,0};
                while(depth){
                    ex.spend(1);const auto arc=stack[--depth];
                    const double x=arc.a.x+arc.b.x,y=arc.a.y+arc.b.y,length=std::sqrt(x*x+y*y);
                    if(!(length>0))invalid(ex,"region offset has an unresolved reversing corner");
                    const Point middle{x/length,y/length};
                    const double sag=radius*std::max(0.0,1-(arc.a.x*middle.x+arc.a.y*middle.y));
                    if(sag<=arcTolerance){append(rounded(ex,{b.x+radius*arc.b.x,b.y+radius*arc.b.y}),join);continue;}
                    if(arc.depth==31 || depth+2>stack.size())invalid(ex,"region round join exceeds subdivision limit");
                    stack[depth++]={middle,arc.b,arc.depth+1};stack[depth++]={arc.a,middle,arc.depth+1};
                }
                // The next shifted straight edge inherits its own provenance.
                incoming=p;
            }else append(to,p);
        }
        if(started)add(last,first,incoming);
    }
    void variableRing(u32 index,i32 id){
        const auto& d=*source;const auto& r=d.rings[index];
        Vec2 first{},last{};EdgeProvenance incoming{};bool started=false;
        const auto append=[&](Vec2 point,EdgeProvenance edge){
            if(!started){first=last=point;started=true;}else{add(last,point,incoming);last=point;}incoming=edge;
        };
        for(u32 k=0;k<r.count;++k){
            ex.spend(36);
            const auto previous=d.uses[r.begin+(k+r.count-1)%r.count],current=d.uses[r.begin+k];
            const auto a=d.start(previous),b=d.start(current),c=d.end(current);
            const auto u=normal(a,b),v=normal(b,c);
            const Profile oldProfile(d,previous.edge,inset,variation,seed),profile(d,current.edge,inset,variation,seed);
            const double fromRadius=oldProfile.radius(oldProfile.parameter(b)),toRadius=profile.radius(profile.parameter(b));
            const auto p=provenance(d,current.edge,id),old=provenance(d,previous.edge,id),join=p.edge<old.edge?p:old;
            append(rounded(ex,{b.x+u.x*fromRadius,b.y+u.y*fromRadius}),join);
            const double bend=turn(a,b,c);
            if(bend>0){append(b,join);append(rounded(ex,{b.x+v.x*toRadius,b.y+v.y*toRadius}),p);}
            else if(bend<0){
                std::array<ProfileArc,64> stack;u32 depth=1;stack[0]={u,v,fromRadius,toRadius,0};
                while(depth){
                    ex.spend(1);const auto arc=stack[--depth];
                    const double x=arc.a.x+arc.b.x,y=arc.a.y+arc.b.y,length=std::sqrt(x*x+y*y);
                    if(!(length>0))invalid(ex,"region gap has an unresolved reversing corner");
                    const Point middle{x/length,y/length};const double radius=(arc.first+arc.last)*0.5;
                    const double cosine=std::clamp(arc.a.x*middle.x+arc.a.y*middle.y,-1.0,1.0);
                    // A radial-linear arc's chord error is bounded by the
                    // circular sag plus the varying-radius tangential term.
                    const double error=std::max(arc.first,arc.last)*(1-cosine)+
                        std::abs(arc.last-arc.first)*std::sqrt(std::max(0.0,1-cosine*cosine))*0.5;
                    if(error<=arcTolerance){append(rounded(ex,{b.x+arc.b.x*arc.last,b.y+arc.b.y*arc.last}),join);continue;}
                    if(arc.depth==31||depth+2>stack.size())invalid(ex,"region gap round join exceeds subdivision limit");
                    stack[depth++]={middle,arc.b,radius,arc.last,arc.depth+1};
                    stack[depth++]={arc.a,middle,arc.first,radius,arc.depth+1};
                }
                incoming=p;
            }else append(rounded(ex,{b.x+v.x*toRadius,b.y+v.y*toRadius}),p);
            // Split at original-edge knots, with canonical interpolation on
            // the current segment, independent of ring traversal direction.
            const bool forward=key(b)<key(c);const auto lo=forward?b:c,hi=forward?c:b;
            const double t0=profile.parameter(lo),t1=profile.parameter(hi);
            for(u32 j=1;j<8;++j){
                ex.spend(1);const u32 knot=(forward==(t1>t0))?j:8-j;const double t=static_cast<double>(knot)/8;
                if(!(t>std::min(t0,t1)&&t<std::max(t0,t1)))continue;
                const double along=(t-t0)/(t1-t0),radius=profile.radii[knot];
                append(rounded(ex,{lo.x+(static_cast<double>(hi.x)-lo.x)*along+v.x*radius,
                    lo.y+(static_cast<double>(hi.y)-lo.y)*along+v.y*radius}),p);
            }
        }
        if(started)add(last,first,incoming);
    }
    void cut(u32 segment,double t,Vec2 p){
        if(++cutVisits>maxCuts)invalid(ex,"region offset exceeds 262144 intersection records");
        ex.spend(1);cuts.push_back({segment,std::clamp(t,0.0,1.0),p});
    }
    double parameter(const Segment& s,Vec2 p)const{
        const auto v=subtract(s.b,s.a);
        return std::abs(v.x)>=std::abs(v.y)?(static_cast<double>(p.x)-s.a.x)/v.x:(static_cast<double>(p.y)-s.a.y)/v.y;
    }
    void intersections(){
        for(u32 i=0;i<raw.size();++i){cut(i,0,raw[i].a);cut(i,1,raw[i].b);}
        for(u32 i=0;i<raw.size();++i)for(u32 j=i+1;j<raw.size();++j){
            ex.spend(1);const auto& a=raw[i];const auto& b=raw[j];
            if(std::max(a.a.x,a.b.x)<std::min(b.a.x,b.b.x)||std::max(b.a.x,b.b.x)<std::min(a.a.x,a.b.x)||
               std::max(a.a.y,a.b.y)<std::min(b.a.y,b.b.y)||std::max(b.a.y,b.b.y)<std::min(a.a.y,a.b.y))continue;
            const auto u=subtract(a.b,a.a),v=subtract(b.b,b.a),w=subtract(b.a,a.a);const double denominator=cross(u,v);
            if(denominator==0){
                if(cross(u,w)!=0)continue;
                for(const auto p:{b.a,b.b}){const double t=parameter(a,p);if(t>=0&&t<=1)cut(i,t,p);}
                for(const auto p:{a.a,a.b}){const double t=parameter(b,p);if(t>=0&&t<=1)cut(j,t,p);}
            }else{
                const double t=cross(w,v)/denominator,s=cross(w,u)/denominator;
                if(t<0||t>1||s<0||s>1)continue;
                // Endpoint precedence prevents rounding a shared endpoint by a
                // second arithmetic path. Both incidences get the same point.
                const Vec2 p=t==0?a.a:t==1?a.b:s==0?b.a:s==1?b.b:
                    rounded(ex,{a.a.x+u.x*t,a.a.y+u.y*t});
                cut(i,t,p);cut(j,s,p);
            }
        }
        geometrySort(ex,cuts,[](const auto& a,const auto& b){return std::tuple(a.segment,a.t,key(a.point))<std::tuple(b.segment,b.t,key(b.point));});
    }
    static int sign(double a,double derivative){return a<0?-1:a>0?1:derivative<0?-1:derivative>0?1:0;}
    int winding(u32 owner,double t,Point direction){
        const auto& own=raw[owner];const auto delta=subtract(own.b,own.a);
        const Point q{own.a.x+delta.x*t,own.a.y+delta.y*t};int result=0;
        for(const auto& edge:raw){
            ex.spend(1);const auto v=subtract(edge.b,edge.a);
            double side=cross(v,{q.x-edge.a.x,q.y-edge.a.y});
            if(turn(own.a,own.b,edge.a)==0&&turn(own.a,own.b,edge.b)==0)side=0;
            const int orientation=sign(side,cross(v,direction));
            const int ay=sign(static_cast<double>(edge.a.y)-q.y,-direction.y),by=sign(static_cast<double>(edge.b.y)-q.y,-direction.y);
            if(ay<=0&&by>0&&orientation>0)++result;
            else if(ay>0&&by<=0&&orientation<0)--result;
        }
        return result;
    }
    void boundaries(){
        for(u32 i=1;i<cuts.size();++i){
            ex.spend(1);const auto& a=cuts[i-1];const auto& b=cuts[i];
            if(a.segment!=b.segment||a.t==b.t||same(a.point,b.point))continue;
            const auto v=subtract(raw[a.segment].b,raw[a.segment].a);const Point right{-v.y,v.x};
            const bool insideRight=winding(a.segment,(a.t+b.t)*0.5,right)>0;
            const bool insideLeft=winding(a.segment,(a.t+b.t)*0.5,{-right.x,-right.y})>0;
            if(insideRight==insideLeft)continue;
            if(++outputVisits>maxRaw)invalid(ex,"region offset exceeds 65536 boundary segments");
            segments.push_back(insideRight?Segment{a.point,b.point,raw[a.segment].provenance}:
                Segment{b.point,a.point,raw[a.segment].provenance});
        }
    }
    static bool angleLess(Point a,Point b){
        const auto half=[](Point v){return v.y<0||(v.y==0&&v.x<0);};
        if(half(a)!=half(b))return half(a)<half(b);
        return cross(a,b)>0;
    }
    void trace(u32 sourceIndex,u32 begin){
        // Equal atom endpoints (overlaps) are a single directed boundary.
        Buffer<Segment,48> order{ResourceAllocator<Segment,48>(ex.resources)};
        ex.spend(segments.size()-begin);order.insert(order.end(),segments.begin()+begin,segments.end());segments.resize(begin);
        geometrySort(ex,order,[](const auto& a,const auto& b){return std::tuple(key(a.a),key(a.b),a.provenance.edge)<std::tuple(key(b.a),key(b.b),b.provenance.edge);});
        for(const auto& s:order){ex.spend(1);if(segments.size()==begin||!same(segments.back().a,s.a)||!same(segments.back().b,s.b))segments.push_back(s);}
        const auto end=static_cast<u32>(segments.size());
        Buffer<u32,4> sorted{ResourceAllocator<u32,4>(ex.resources)};
        for(u32 i=begin;i<end;++i)sorted.push_back(i);
        // segments already sorts by start; binary search finds outgoing edges.
        for(u32 i=begin;i<end;++i){
            auto& s=segments[i];const auto target=key(s.b);std::size_t lo=0,hi=sorted.size();
            while(lo<hi){ex.spend(1);const auto mid=(lo+hi)/2;if(key(segments[sorted[mid]].a)<target)lo=mid+1;else hi=mid;}
            u32 selected=kNoGeometryIndex;const auto reverse=subtract(s.a,s.b);
            for(auto j=lo;j<sorted.size()&&key(segments[sorted[j]].a)==target;++j){
                ex.spend(1);const auto candidate=sorted[j];const auto direction=subtract(segments[candidate].b,s.b);
                if(selected==kNoGeometryIndex)selected=candidate;
                else{
                    const auto best=subtract(segments[selected].b,s.b);
                    // Predecessor of the reverse incoming ray in angular order.
                    const bool before=angleLess(direction,reverse),bestBefore=angleLess(best,reverse);
                    if((before&&!bestBefore)||(before==bestBefore&&angleLess(best,direction)))selected=candidate;
                }
            }
            if(selected==kNoGeometryIndex)invalid(ex,"region offset boundary cannot be closed after f32 rounding");s.next=selected;
        }
        Buffer<Segment,48> closed{ResourceAllocator<Segment,48>(ex.resources)};
        for(u32 i=begin;i<end;++i){
            ex.spend(1);if(segments[i].used)continue;const auto start=static_cast<u32>(closed.size());auto j=i;
            do{ex.spend(1);auto& s=segments[j];if(s.used)invalid(ex,"region offset has an ambiguous touching boundary");s.used=true;closed.push_back(s);j=s.next;}while(j!=i);
            // Remove exactly collinear forward vertices. Provenance changes
            // remain explicit, even along straight lines.
            bool changed=true;
            while(changed&&closed.size()-start>=3){
                changed=false;const auto n=static_cast<u32>(closed.size())-start;
                for(u32 k=0;k<n;++k){
                    ex.spend(1);const auto prev=start+(k+n-1)%n,at=start+k;auto& a=closed[prev];const auto b=closed[at];
                    if(a.provenance.edge==b.provenance.edge&&turn(a.a,b.a,b.b)==0&&
                       (static_cast<double>(b.a.x)-a.a.x)*(static_cast<double>(b.b.x)-b.a.x)+
                       (static_cast<double>(b.a.y)-a.a.y)*(static_cast<double>(b.b.y)-b.a.y)>0){
                        a.b=b.b;for(u32 z=at;z+1<closed.size();++z){ex.spend(1);closed[z]=closed[z+1];}
                        closed.resize(closed.size()-1);changed=true;break;
                    }
                }
            }
            const auto n=static_cast<u32>(closed.size())-start;
            if(n<3){closed.resize(start);continue;}
            if(n>kMaxRingVertices)invalid(ex,"region offset ring exceeds 2048 vertices");
            Loop loop;loop.begin=start;loop.count=n;loop.source=sourceIndex;loop.discovery=static_cast<u32>(loops.size());loop.lo=loop.hi=closed[start].a;
            u32 canonical=start;double twice=0;const auto reference=closed[start].a;
            for(u32 k=0;k<n;++k){
                ex.spend(1);const auto& s=closed[start+k];twice+=cross(subtract(s.a,reference),subtract(s.b,reference));
                loop.lo={std::min(loop.lo.x,s.a.x),std::min(loop.lo.y,s.a.y)};loop.hi={std::max(loop.hi.x,s.a.x),std::max(loop.hi.y,s.a.y)};
                if(key(s.a)<key(closed[canonical].a))canonical=start+k;
            }
            loop.area=twice*0.5;loop.net=loop.area;
            if(loop.area==0){closed.resize(start);continue;}
            // Rotate through a bounded scratch range; no recursive std sort.
            Buffer<Segment,48> rotated{ResourceAllocator<Segment,48>(ex.resources)};rotated.reserve(n);
            for(u32 k=0;k<n;++k){ex.spend(1);rotated.push_back(closed[start+(canonical-start+k)%n]);}
            ex.spend(n);for(u32 k=0;k<n;++k)closed[start+k]=rotated[k];
            loops.push_back(loop);
        }
        segments.resize(begin);
        for(auto& loop:loops)if(loop.source==sourceIndex){loop.begin+=begin;}
        ex.spend(closed.size());segments.insert(segments.end(),closed.begin(),closed.end());
    }
    void validate(u32 begin,u32 firstLoop){
        ex.spend(segments.size()-begin);
        Buffer<u32,4> order{ResourceAllocator<u32,4>(ex.resources)},owners(segments.size()-begin,kNoGeometryIndex,ResourceAllocator<u32,4>(ex.resources));
        for(u32 i=firstLoop;i<loops.size();++i)for(u32 k=0;k<loops[i].count;++k){ex.spend(1);owners[loops[i].begin+k-begin]=i;}
        for(u32 i=begin;i<segments.size();++i){ex.spend(1);order.push_back(i);}
        geometrySort(ex,order,[&](u32 a,u32 b){return std::pair(std::min(segments[a].a.x,segments[a].b.x),a)<std::pair(std::min(segments[b].a.x,segments[b].b.x),b);});
        const auto on=[](Vec2 a,Vec2 b,Vec2 p){return p.x>=std::min(a.x,b.x)&&p.x<=std::max(a.x,b.x)&&p.y>=std::min(a.y,b.y)&&p.y<=std::max(a.y,b.y);};
        for(u32 i=0;i<order.size();++i)for(u32 j=i+1;j<order.size();++j){
            ex.spend(1);const auto left=order[i],right=order[j];const auto a=segments[left],b=segments[right];
            if(std::min(b.a.x,b.b.x)>std::max(a.a.x,a.b.x))break;
            if(std::max(a.a.y,a.b.y)<std::min(b.a.y,b.b.y)||std::max(b.a.y,b.b.y)<std::min(a.a.y,a.b.y))continue;
            const double ac=turn(a.a,a.b,b.a),ad=turn(a.a,a.b,b.b),ca=turn(b.a,b.b,a.a),cb=turn(b.a,b.b,a.b);
            if(ac==0&&ad==0){
                const bool horizontal=a.a.x!=a.b.x;
                const double lo=horizontal?std::max(std::min(a.a.x,a.b.x),std::min(b.a.x,b.b.x)):std::max(std::min(a.a.y,a.b.y),std::min(b.a.y,b.b.y));
                const double hi=horizontal?std::min(std::max(a.a.x,a.b.x),std::max(b.a.x,b.b.x)):std::min(std::max(a.a.y,a.b.y),std::max(b.a.y,b.b.y));
                if(hi>lo)invalid(ex,"region offset overlaps itself after f32 rounding");
            }
            const bool hit=(ac==0&&on(a.a,a.b,b.a))||(ad==0&&on(a.a,a.b,b.b))||
                (ca==0&&on(b.a,b.b,a.a))||(cb==0&&on(b.a,b.b,a.b))||
                (((ac>0&&ad<0)||(ac<0&&ad>0))&&((ca>0&&cb<0)||(ca<0&&cb>0)));
            if(!hit)continue;
            const bool endpoint=same(a.a,b.a)||same(a.a,b.b)||same(a.b,b.a)||same(a.b,b.b);
            bool allowed=endpoint&&owners[left-begin]!=owners[right-begin];
            if(endpoint&&owners[left-begin]==owners[right-begin]){
                const auto& loop=loops[owners[left-begin]];const auto x=left-loop.begin,y=right-loop.begin;
                allowed=(x+1)%loop.count==y||(y+1)%loop.count==x;
            }
            if(!allowed)invalid(ex,"region offset intersects itself after f32 rounding");
        }
    }
    bool contains(const Loop& loop,Vec2 p){
        bool inside=false;
        for(u32 k=0;k<loop.count;++k){ex.spend(1);const auto& edge=segments[loop.begin+k];
            const auto a=edge.a,b=edge.b;if((a.y>p.y)!=(b.y>p.y)){
                const double x=a.x+(static_cast<double>(p.y)-a.y)*(static_cast<double>(b.x)-a.x)/(static_cast<double>(b.y)-a.y);
                if(p.x<x)inside=!inside;
            }
        }return inside;
    }
    void hierarchy(u32 begin){
        for(u32 i=begin;i<loops.size();++i){
            auto& l=loops[i];if(l.area>0)continue;u32 parent=kNoGeometryIndex;
            for(u32 j=begin;j<loops.size();++j){ex.spend(1);const auto& p=loops[j];if(p.area<=0||p.area<=-l.area)continue;
                if(l.lo.x<p.lo.x||l.lo.y<p.lo.y||l.hi.x>p.hi.x||l.hi.y>p.hi.y)continue;
                if(contains(p,segments[l.begin].a)&&(parent==kNoGeometryIndex||p.area<loops[parent].area))parent=j;
            }
            if(parent==kNoGeometryIndex)invalid(ex,"region offset hole has no enclosing component");l.parent=parent;loops[parent].net+=l.area;
        }
    }
    void run(){
        for(u32 i=0;i<source->regions.size();++i){
            ex.spend(1);raw.clear();cuts.clear();const auto begin=static_cast<u32>(segments.size()),loopBegin=static_cast<u32>(loops.size());
            const auto emit=[&](u32 index){if(variation>0)variableRing(index,source->regions[i].sourceId);else ring(index,source->regions[i].sourceId);};
            const auto& r=source->regions[i];emit(r.outer);for(const auto h:source->regionHoles(i))emit(h);
            intersections();boundaries();trace(i,begin);validate(begin,loopBegin);hierarchy(loopBegin);
        }
    }
    auto fragmentKey(u32 index)const{const auto& l=loops[index];return std::tuple(l.source,-l.net,l.lo.y,l.lo.x,l.hi.y,l.hi.x,l.discovery);}
    std::shared_ptr<const GeometryData> publish(f32 minimum){
        Buffer<u32,4> order{ResourceAllocator<u32,4>(ex.resources)};
        for(u32 i=0;i<loops.size();++i){ex.spend(1);auto& l=loops[i];
            if(l.area>0){if(!(l.net>0))invalid(ex,"region offset component has nonpositive net area");if(l.net>=minimum){l.keep=true;order.push_back(i);}}}
        if(order.size()>kMaxGeometryRegions)invalid(ex,"region offset exceeds 256 regions after min_area");
        geometrySort(ex,order,[&](u32 a,u32 b){return fragmentKey(a)<fragmentKey(b);});
        GeometryBuilder builder(ex,GeometryOrigin::Offset,source);
        Buffer<u32,4> outers{ResourceAllocator<u32,4>(ex.resources)},holes{ResourceAllocator<u32,4>(ex.resources)};
        ex.spend(loops.size());
        Buffer<u32,4> mapped(loops.size(),kNoGeometryIndex,ResourceAllocator<u32,4>(ex.resources));
        Buffer<GeometryEdgeUse,8> uses{ResourceAllocator<GeometryEdgeUse,8>(ex.resources)};
        Buffer<Link,16> links{ResourceAllocator<Link,16>(ex.resources)};
        const auto emit=[&](u32 index,u32 parent,u32 outputIndex){
            const auto& l=loops[index];uses.clear();
            const auto first=builder.point(segments[l.begin].a);
            for(u32 k=1;k<l.count;++k)builder.point(segments[l.begin+k].a);
            for(u32 k=0;k<l.count;++k){const auto a=first+k,b=first+(k+1)%l.count;
                const auto p=segments[l.begin+k].provenance;
                const auto edge=builder.edge(std::min(a,b),std::max(a,b),p);uses.push_back({edge,a>b});
                if(p.neighbor>=0){ex.spend(1);links.push_back({p.edge,outputIndex,p.source,p.neighbor});}}
            mapped[index]=builder.ring(uses,parent);
        };
        for(u32 at=0;at<order.size();++at){const auto i=order[at];emit(i,kNoGeometryIndex,at);outers.push_back(mapped[i]);
            for(u32 h=0;h<loops.size();++h){ex.spend(1);if(loops[h].parent==i)emit(h,mapped[i],at);}}
        geometrySort(ex,links,[](const auto& a,const auto& b){return std::tuple(a.edge,a.region,a.neighbor)<std::tuple(b.edge,b.region,b.neighbor);});
        u32 unique=0;
        for(const auto link:links){ex.spend(1);if(!unique||links[unique-1].edge!=link.edge||links[unique-1].region!=link.region||links[unique-1].neighbor!=link.neighbor)links[unique++]=link;}
        links.resize(unique);const auto count=static_cast<u32>(order.size());const auto bytes=(count*count+7)/8;ex.spend(bytes);
        Buffer<u8,1> adjacent(bytes,u8{0},ResourceAllocator<u8,1>(ex.resources));
        const auto set=[&](u32 a,u32 b){const auto bit=a*count+b;adjacent[bit/8]|=static_cast<u8>(1u<<(bit%8));};
        for(u32 begin=0;begin<links.size();){u32 end=begin+1;while(end<links.size()&&links[end].edge==links[begin].edge){ex.spend(1);++end;}
            for(u32 i=begin;i<end;++i)for(u32 j=i+1;j<end;++j){ex.spend(1);const auto a=links[i],b=links[j];
                if(a.region!=b.region&&a.source!=b.source&&a.neighbor==b.source&&b.neighbor==a.source){set(a.region,b.region);set(b.region,a.region);}}
            begin=end;
        }
        Buffer<i32,4> neighbors{ResourceAllocator<i32,4>(ex.resources)};
        for(u32 at=0;at<order.size();++at){
            const auto i=order[at];holes.clear();neighbors.clear();
            for(u32 h=0;h<loops.size();++h){ex.spend(1);if(loops[h].parent==i)holes.push_back(mapped[h]);}
            for(u32 other=0;other<count;++other){ex.spend(1);const auto bit=at*count+other;if(adjacent[bit/8]&(1u<<(bit%8)))neighbors.push_back(static_cast<i32>(other));}
            builder.region(outers[at],source->regions[loops[i].source].sourceId,holes,neighbors);
        }
        return std::move(builder).finish();
    }
};
}
std::shared_ptr<const GeometryData> offsetRegions(Execution& ex,const std::shared_ptr<const GeometryData>& source,f32 inset,f32 minimum){
    if(!source)ex.trap("region source is unassigned",ErrorReason::RuntimeViolation);
    if(!std::isfinite(inset))invalid(ex,"regions inset must be finite");
    if(!std::isfinite(minimum)||minimum<0)invalid(ex,"regions min_area must be finite and nonnegative");
    if(inset==0 || source->regions.empty())return geometryRegions(ex,source,minimum);
    ex.resources.resourceLifetime()->location=ex.activeInstruction;const ResourceReservation workspace(ex.resources,8192);
    Offset offset(ex,source,inset);offset.run();return offset.publish(minimum);
}
std::shared_ptr<const GeometryData> gapRegions(Execution& ex,const std::shared_ptr<const GeometryData>& source,f32 width,f32 variation,i32 seed){
    if(!source)ex.trap("region source is unassigned",ErrorReason::RuntimeViolation);
    if(!std::isfinite(width)||width<0)invalid(ex,"regions gap width must be finite and nonnegative");
    if(!std::isfinite(variation)||variation<0||variation>1)invalid(ex,"regions gap variation must be finite in [0,1]");
    const f32 half=width*0.5f;
    if(variation==0||half==0||source->regions.empty())return offsetRegions(ex,source,half);
    ex.resources.resourceLifetime()->location=ex.activeInstruction;const ResourceReservation workspace(ex.resources,12288);
    Offset offset(ex,source,half);offset.variation=variation;offset.seed=seed;offset.run();return offset.publish(0);
}
} // namespace iprop::detail
