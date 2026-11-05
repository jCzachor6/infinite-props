#pragma once
#include "../../ResultQueries.hpp"
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/packed_vector2_array.hpp>
#include <godot_cpp/variant/vector2.hpp>
#include <cstring>

namespace iprop_godot {
inline godot::Vector2 point(iprop_point_value p) { return {p.x, p.y}; }
inline bool partValue(const iprop_part_snapshot_ex& p, godot::Dictionary& v) {
#define FIELD(name) v[#name] = static_cast<int64_t>(p.name)
    FIELD(output_index); FIELD(element_index); FIELD(id); FIELD(source_id);
    FIELD(shape_kind); FIELD(appearance_kind); FIELD(shape_resource); FIELD(region_index); FIELD(texture_resource);
    FIELD(d4); FIELD(translation_x); FIELD(translation_y);
    FIELD(source_d4); FIELD(source_origin_x); FIELD(source_origin_y);
    FIELD(texture_d4); FIELD(texture_origin_x); FIELD(texture_origin_y);
#undef FIELD
    godot::PackedByteArray tint;
    if (tint.resize(4) != godot::OK) return false;
    const std::uint8_t rgba[] = {p.tint.r, p.tint.g, p.tint.b, p.tint.a};
    std::memcpy(tint.ptrw(), rgba, 4); v["tint"] = tint;
    v["source_scale"] = point(p.source_scale); v["target_scale"] = point(p.target_scale);
    v["source_anchor"] = point(p.source_anchor); v["center"] = point(p.center);
    v["bounds_origin"] = point(p.bounds_origin); v["bounds_size"] = point(p.bounds_size);
    return true;
}
inline bool resourceValues(const std::vector<iprop::backend::ResultResource>& stores, godot::Array& list) {
    for (const auto& store : stores) {
        const auto& r = store.info; godot::Dictionary v;
        v["id"] = static_cast<int64_t>(r.resource_id); v["kind"] = static_cast<int64_t>(r.kind);
        v["pixel_format"] = static_cast<int64_t>(r.pixel_format); v["width"] = static_cast<int64_t>(r.width);
        v["height"] = static_cast<int64_t>(r.height); v["row_stride"] = static_cast<int64_t>(r.row_stride);
        v["byte_count"] = static_cast<int64_t>(r.byte_count); v["scale"] = point(r.scale);
        v["vertex_count"] = static_cast<int64_t>(r.vertex_count); v["edge_count"] = static_cast<int64_t>(r.edge_count);
        godot::PackedByteArray data;
        if (data.resize(static_cast<int64_t>(store.pixels.size())) != godot::OK) return false;
        if (!store.pixels.empty()) std::memcpy(data.ptrw(), store.pixels.data(), store.pixels.size());
        v["data"] = data;
        godot::Array rings, regions;
        for (const auto& ring : store.rings) {
            godot::Dictionary entry; godot::PackedVector2Array vertices;
            if (vertices.resize(static_cast<int64_t>(ring.vertices.size())) != godot::OK) return false;
            for (std::size_t i = 0; i < ring.vertices.size(); ++i) vertices.set(static_cast<int64_t>(i), point(ring.vertices[i]));
            entry["index"] = static_cast<int64_t>(ring.info.ring_index); entry["parent_ring"] = static_cast<int64_t>(ring.info.parent_ring);
            entry["is_hole"] = ring.info.is_hole != 0; entry["vertices"] = vertices; rings.append(entry);
        }
        for (const auto& region : store.regions) {
            const auto& info = region.info; godot::Dictionary entry; godot::Array holes;
            entry["index"] = static_cast<int64_t>(info.region_index); entry["outer_ring"] = static_cast<int64_t>(info.outer_ring);
            entry["source_id"] = static_cast<int64_t>(info.source_id); entry["area"] = info.area;
            entry["center"] = point(info.center); entry["bounds_origin"] = point(info.bounds_origin); entry["bounds_size"] = point(info.bounds_size);
            for (const auto hole : region.holes) holes.append(static_cast<int64_t>(hole));
            entry["holes"] = holes; regions.append(entry);
        }
        v["rings"] = rings; v["regions"] = regions; list.append(v);
    }
    return true;
}
} // namespace iprop_godot
