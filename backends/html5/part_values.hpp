// JavaScript values for the shared C descriptors. Buffers are owned JS copies.
#pragma once
#include "../ResultQueries.hpp"
#include <emscripten/val.h>

namespace iprop::html5 {
using emscripten::val;
inline val point(iprop_point_value p) {
    auto v = val::object(); v.set("x", p.x); v.set("y", p.y); return v;
}
inline val part(const iprop_part_snapshot_ex& p) {
    auto v = val::object();
#define FIELD(name, field) v.set(name, p.field)
    FIELD("outputIndex", output_index); FIELD("elementIndex", element_index);
    FIELD("id", id); FIELD("sourceId", source_id);
    FIELD("shapeKind", shape_kind); FIELD("appearanceKind", appearance_kind);
    FIELD("shapeResource", shape_resource); FIELD("regionIndex", region_index); FIELD("textureResource", texture_resource);
    FIELD("d4", d4); FIELD("translationX", translation_x); FIELD("translationY", translation_y);
    FIELD("sourceD4", source_d4); FIELD("sourceOriginX", source_origin_x); FIELD("sourceOriginY", source_origin_y);
    FIELD("textureD4", texture_d4); FIELD("textureOriginX", texture_origin_x); FIELD("textureOriginY", texture_origin_y);
#undef FIELD
    auto tint = val::array();
    for (const auto channel : {p.tint.r, p.tint.g, p.tint.b, p.tint.a}) tint.call<void>("push", channel);
    v.set("tint", tint);
    v.set("sourceScale", point(p.source_scale)); v.set("targetScale", point(p.target_scale));
    v.set("sourceAnchor", point(p.source_anchor)); v.set("center", point(p.center));
    v.set("boundsOrigin", point(p.bounds_origin)); v.set("boundsSize", point(p.bounds_size));
    return v;
}
inline val resources(const std::vector<backend::ResultResource>& stores) {
    auto list = val::array();
    for (const auto& store : stores) {
        const auto& r = store.info;
        auto v = val::object();
        v.set("id", r.resource_id); v.set("kind", r.kind); v.set("pixelFormat", r.pixel_format);
        v.set("width", r.width); v.set("height", r.height);
        v.set("rowStride", static_cast<double>(r.row_stride)); v.set("byteCount", static_cast<double>(r.byte_count));
        v.set("scale", point(r.scale)); v.set("vertexCount", r.vertex_count); v.set("edgeCount", r.edge_count);
        // slice() detaches storage from both the C result and the WASM heap.
        v.set("data", val(emscripten::typed_memory_view(store.pixels.size(), store.pixels.data())).call<val>("slice"));
        auto rings = val::array(), regions = val::array();
        for (const auto& ring : store.rings) {
            auto entry = val::object(), vertices = val::array();
            entry.set("index", ring.info.ring_index); entry.set("parentRing", ring.info.parent_ring);
            entry.set("isHole", ring.info.is_hole != 0);
            for (const auto p : ring.vertices) vertices.call<void>("push", point(p));
            entry.set("vertices", vertices); rings.call<void>("push", entry);
        }
        for (const auto& region : store.regions) {
            const auto& info = region.info;
            auto entry = val::object(), holes = val::array();
            entry.set("index", info.region_index); entry.set("outerRing", info.outer_ring); entry.set("sourceId", info.source_id);
            entry.set("area", info.area); entry.set("center", point(info.center));
            entry.set("boundsOrigin", point(info.bounds_origin)); entry.set("boundsSize", point(info.bounds_size));
            for (const auto hole : region.holes) holes.call<void>("push", hole);
            entry.set("holes", holes); regions.call<void>("push", entry);
        }
        v.set("rings", rings); v.set("regions", regions); list.call<void>("push", v);
    }
    return list;
}
} // namespace iprop::html5
