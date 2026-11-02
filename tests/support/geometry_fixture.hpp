#pragma once

// Hand-authored topology, independent of the later Voronoi/contour algorithms.
// Also compiled by the native/WASM representation probe.
#include "iprop/Geometry.hpp"
#include "iprop/VmInternals.hpp"
#include <array>

namespace geometry_fixture {
using namespace iprop;

inline std::shared_ptr<const GeometryData> adjacent(detail::Execution& ex) {
    detail::GeometryBuilder b(ex, GeometryOrigin::Diagram);
    for (const Vec2 p : {Vec2{0,0}, {2,0}, {4,0}, {4,2}, {2,2}, {0,2}}) b.point(p);
    b.site({1,1}); b.site({3,1});
    b.edge(0,1); b.edge(1,4,{1,0,1,EdgeOrigin::Diagram}); b.edge(4,5); b.edge(0,5);
    b.edge(1,2); b.edge(2,3); b.edge(3,4);
    const std::array<GeometryEdgeUse,4> left{{{0,false},{1,false},{2,false},{3,true}}};
    const std::array<GeometryEdgeUse,4> right{{{4,false},{5,false},{6,false},{1,true}}};
    b.ring(left); b.ring(right);
    const std::array<i32,1> neighborLeft{1}, neighborRight{0};
    b.region(0,0,{},neighborLeft); b.region(1,1,{},neighborRight);
    return std::move(b).finish();
}

inline u32 rectangle(detail::GeometryBuilder& b, Vec2 lo, Vec2 hi, u32 parent = kNoGeometryIndex, bool hole = false) {
    const u32 p = b.point(lo);
    b.point({hi.x,lo.y}); b.point(hi); b.point({lo.x,hi.y});
    const u32 e = b.edge(p,p+1);
    b.edge(p+1,p+2); b.edge(p+2,p+3); b.edge(p,p+3);
    const std::array<GeometryEdgeUse,4> positive{{{e,false},{e+1,false},{e+2,false},{e+3,true}}};
    const std::array<GeometryEdgeUse,4> negative{{{e+3,false},{e+2,true},{e+1,true},{e,true}}};
    return b.ring(hole ? negative : positive, parent);
}

inline std::shared_ptr<const GeometryData> holes(detail::Execution& ex) {
    detail::GeometryBuilder b(ex, GeometryOrigin::Contours);
    const auto outer = rectangle(b,{0,0},{10,8});
    const auto hole = rectangle(b,{2,2},{4,4},outer,true);
    const auto island = rectangle(b,{2.5f,2.5f},{3.5f,3.5f},hole);
    const std::array<u32,1> holes{hole};
    b.region(outer,9,holes); b.region(island,10);
    return std::move(b).finish();
}
inline std::shared_ptr<const GeometryData> fractional(detail::Execution& ex) {
    detail::GeometryBuilder b(ex, GeometryOrigin::Contours);
    b.point({-3.5f,-1.25f});b.point({1.25f,-0.5f});b.point({-0.75f,2.75f});
    b.edge(0,1);b.edge(1,2);b.edge(0,2);
    const std::array<GeometryEdgeUse,3> uses{{{0,false},{1,false},{2,true}}};
    b.ring(uses);b.region(0,73);
    return std::move(b).finish();
}
} // namespace geometry_fixture
