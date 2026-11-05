#include "iprop.h"
#include "CheckedBoundary.hpp"
#include "Handles.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <type_traits>

namespace {
using namespace iprop;
using namespace iprop::capi;

struct Resource {
    const GeometryData* geometry = nullptr;
    const SurfaceBuffer* raster = nullptr;
    bool operator==(const Resource&) const = default;
};

// Queries never allocate or retain another copy of geometry/pixels. This
// bounded scratch enumerates first use, not pointer-sorted/address order.
struct Resources {
    std::array<Resource, kMaxExportedParts * 2> entries{};
    uint32_t count = 0;
    explicit Resources(const iprop_result* result) noexcept {
        if (!result) return;
        const auto add = [&](Resource value) {
            if (!id(value)) entries[count++] = value;
        };
        for (const auto& slot : result->partSlots) for (const auto& snapshot : slot.data->records) {
            const auto& part = *snapshot.part.data;
            std::visit([&](const auto& source) {
                if constexpr (std::is_same_v<std::decay_t<decltype(source)>, RegionSource>)
                    add({source.geometry.get(), nullptr});
                else add({nullptr, source.buffer.get()});
            }, part.source);
            if (part.texture) add({nullptr, part.texture->buffer.get()});
        }
    }
    uint32_t id(Resource value) const noexcept {
        for (uint32_t i=0; i<count; ++i) if (entries[i] == value) return i+1;
        return 0;
    }
    Resource get(uint32_t id) const noexcept { return id && id <= count ? entries[id-1] : Resource{}; }
};

#define SLOT_FIELDS(X) X(output_index) X(owner) X(kind) X(is_list) X(element_count)
#define PART_FIELDS(X) X(output_index) X(element_index) X(id) X(source_id) X(shape_kind) X(appearance_kind) \
    X(shape_resource) X(region_index) X(texture_resource) X(tint) X(d4) X(translation_x) X(translation_y) \
    X(source_scale) X(target_scale) X(source_anchor) X(center) X(bounds_origin) X(bounds_size) \
    X(source_d4) X(source_origin_x) X(source_origin_y) X(texture_d4) X(texture_origin_x) X(texture_origin_y)
#define RESOURCE_FIELDS(X) X(resource_id) X(kind) X(pixel_format) X(width) X(height) X(row_stride) \
    X(byte_count) X(data) X(scale) X(vertex_count) X(edge_count) X(ring_count) X(region_count)
#define RING_FIELDS(X) X(ring_index) X(vertex_count) X(parent_ring) X(is_hole)
#define REGION_FIELDS(X) X(region_index) X(outer_ring) X(hole_count) X(source_id) X(area) \
    X(center) X(bounds_origin) X(bounds_size)

#define WRITE(field) writeField(out, capacity, offsetof(Descriptor, field), value.field);
#define DEFINE_WRITER(Type, Fields) \
    void writeDescriptor(Type* out, uint32_t capacity, const Type& value) noexcept { \
        using Descriptor = Type; Fields(WRITE) \
    }
DEFINE_WRITER(iprop_part_snapshot_ex, PART_FIELDS)
DEFINE_WRITER(iprop_resource_info_ex, RESOURCE_FIELDS)
DEFINE_WRITER(iprop_ring_info_ex, RING_FIELDS)
DEFINE_WRITER(iprop_region_info_ex, REGION_FIELDS)
void writeDescriptor(iprop_output_slot_ex* out, uint32_t capacity, const iprop_output_slot_ex& value) noexcept {
    using Descriptor = iprop_output_slot_ex;
    SLOT_FIELDS(WRITE)
    writeField(out, capacity, offsetof(Descriptor,name)+offsetof(iprop_utf8_view,data), value.name.data);
    writeField(out, capacity, offsetof(Descriptor,name)+offsetof(iprop_utf8_view,byte_count), value.name.byte_count);
}
#undef DEFINE_WRITER
#undef WRITE

template<class Descriptor> iprop_status_ex initialize(Descriptor* out, uint32_t capacity) noexcept {
    if (!out || capacity < sizeof(uint32_t)) return IPROP_EX_E_ARGUMENT;
    writeField(out, capacity, 0, capacity);
    const Descriptor value{};
    writeField(out, capacity, offsetof(Descriptor, flags), value.flags);
    writeDescriptor(out, capacity, value);
    return IPROP_EX_OK;
}

template<class Descriptor, class Query>
iprop_status_ex sized(const iprop_result* result, Descriptor* out, iprop_error_ex* error, Query query) noexcept {
    ErrorContext context{ErrorStage::Arguments};
    return checkedBoundary(error, out, queryCapacity(out), context, [&] {
        if (!result) return CallOutcome::argument("result is null");
        uint32_t capacity=0;
        if (!descriptorSize(out,capacity)) return CallOutcome::argument("descriptor is null or smaller than four bytes");
        if (readField<uint32_t>(out,capacity,offsetof(Descriptor,flags)))
            return CallOutcome::argument("descriptor flags are unsupported");
        Descriptor value{};
        const auto status=query(value);
        if (!status.succeeded) return status;
        writeDescriptor(out,capacity,value);
        return CallOutcome{};
    });
}

iprop_point_value point(Vec2 value) noexcept { return {value.x,value.y}; }
const OutputPart* snapshot(const iprop_result* result, uint32_t output, uint32_t index) noexcept {
    for (const auto& slot : result->partSlots) if (slot.outputIndex == output)
        return index < slot.data->records.size() ? &slot.data->records[index] : nullptr;
    return nullptr;
}
uint64_t rasterBytes(const SurfaceBuffer& surface) noexcept {
    return uint64_t{surface.raster.width} * surface.raster.height *
        (surface.format == SurfaceBuffer::Format::A8 ? 1u : 4u);
}
const uint8_t* rasterData(const SurfaceBuffer& surface) noexcept {
    return surface.format == SurfaceBuffer::Format::A8 ? surface.coverage.data() :
        reinterpret_cast<const uint8_t*>(surface.raster.pixels.data());
}
std::size_t capacitySize(uint64_t capacity) noexcept {
    return static_cast<std::size_t>(std::min<uint64_t>(capacity,std::numeric_limits<std::size_t>::max()));
}
} // namespace

extern "C" iprop_status_ex iprop_output_slot_ex_init(iprop_output_slot_ex* out,uint32_t capacity) {return initialize(out,capacity);}
extern "C" iprop_status_ex iprop_part_snapshot_ex_init(iprop_part_snapshot_ex* out,uint32_t capacity) {return initialize(out,capacity);}
extern "C" iprop_status_ex iprop_resource_info_ex_init(iprop_resource_info_ex* out,uint32_t capacity) {return initialize(out,capacity);}
extern "C" iprop_status_ex iprop_ring_info_ex_init(iprop_ring_info_ex* out,uint32_t capacity) {return initialize(out,capacity);}
extern "C" iprop_status_ex iprop_region_info_ex_init(iprop_region_info_ex* out,uint32_t capacity) {return initialize(out,capacity);}

extern "C" iprop_status_ex iprop_result_output_slot(const iprop_result* result,uint32_t output_index,
                                                    iprop_output_slot_ex* out,iprop_error_ex* error) {
    return sized(result,out,error,[&](iprop_output_slot_ex& view) {
        if(output_index>=result->outputs.size())return CallOutcome::argument("output index is out of range");
        const auto& slot=result->outputs[output_index];
        view.output_index=output_index;view.owner=slot.owner;view.kind=static_cast<uint32_t>(slot.kind);
        view.is_list=slot.is_list;view.element_count=slot.count;
        view.name={slot.name,static_cast<uint32_t>(result->names[output_index].size())};
        return CallOutcome{};
    });
}

extern "C" iprop_status_ex iprop_result_output_part(const iprop_result* result,uint32_t output_index,
                                                    uint32_t element_index,iprop_part_snapshot_ex* out,
                                                    iprop_error_ex* error) {
    return sized(result,out,error,[&](iprop_part_snapshot_ex& view) {
        if(output_index>=result->outputs.size())return CallOutcome::argument("output index is out of range");
        if(static_cast<uint32_t>(result->outputs[output_index].kind)!=IPROP_OUTPUT_KIND_PART)
            return CallOutcome::argument("output is not a part list");
        const auto* entry=snapshot(result,output_index,element_index);
        if(!entry)return CallOutcome::argument("part element index is out of range");
        const auto& p=*entry->part.data;const Resources resources(result);
        view.output_index=output_index;view.element_index=entry->index;view.id=p.id;view.source_id=p.sourceId;
        view.region_index=IPROP_NO_REGION;
        std::visit([&](const auto& source) {
            if constexpr(std::is_same_v<std::decay_t<decltype(source)>,RegionSource>) {
                view.shape_kind=IPROP_PART_SHAPE_REGION;view.shape_resource=resources.id({source.geometry.get(),nullptr});
                view.region_index=source.region;
            } else {
                view.shape_kind=std::is_same_v<std::decay_t<decltype(source)>,MaskValue>?IPROP_PART_SHAPE_MASK:IPROP_PART_SHAPE_LAYER;
                view.shape_resource=resources.id({nullptr,source.buffer.get()});
                view.source_d4=source.mapping.transform.kind();
                view.source_origin_x=source.mapping.origin.x;view.source_origin_y=source.mapping.origin.y;
            }
        },p.source);
        if(p.fill) {
            view.appearance_kind=IPROP_PART_APPEARANCE_FILL;
            view.tint={p.fill->r,p.fill->g,p.fill->b,p.fill->a};
        } else if(p.texture) {
            view.appearance_kind=IPROP_PART_APPEARANCE_TEXTURE;
            view.texture_resource=resources.id({nullptr,p.texture->buffer.get()});
            view.texture_d4=p.texture->mapping.transform.kind();
            view.texture_origin_x=p.texture->mapping.origin.x;view.texture_origin_y=p.texture->mapping.origin.y;
        } else if(std::holds_alternative<LayerValue>(p.source))view.appearance_kind=IPROP_PART_APPEARANCE_SOURCE_RGBA;
        view.d4=entry->transform.kind();view.translation_x=entry->translation.x;view.translation_y=entry->translation.y;
        view.source_scale=point(entry->sourceScale);view.target_scale=point(entry->targetScale);
        view.source_anchor={static_cast<float>(p.original.centerX/p.original.scale.x),
                            static_cast<float>(p.original.centerY/p.original.scale.y)};
        view.center=point(entry->center);view.bounds_origin=point(entry->boundsOrigin);view.bounds_size=point(entry->boundsSize);
        return CallOutcome{};
    });
}

extern "C" iprop_status_ex iprop_result_resource_count(const iprop_result* result,uint32_t* out_count,iprop_error_ex* error) {
    ErrorContext context{ErrorStage::Arguments};
    return checkedBoundary(error,out_count,sizeof(*out_count),context,[&] {
        if(!result||!out_count)return CallOutcome::argument("result or count is null");
        *out_count=Resources(result).count;return CallOutcome{};
    });
}

extern "C" iprop_status_ex iprop_result_resource_get(const iprop_result* result,uint32_t resource_id,
                                                     iprop_resource_info_ex* out,iprop_error_ex* error) {
    return sized(result,out,error,[&](iprop_resource_info_ex& view) {
        const auto resource=Resources(result).get(resource_id);
        if(!resource.geometry&&!resource.raster)return CallOutcome::argument("resource ID is out of range");
        view.resource_id=resource_id;
        if(resource.geometry) {
            const auto& g=*resource.geometry;view.kind=IPROP_RESOURCE_GEOMETRY;
            view.vertex_count=static_cast<uint32_t>(g.points.size());view.edge_count=static_cast<uint32_t>(g.edges.size());
            view.ring_count=static_cast<uint32_t>(g.rings.size());view.region_count=static_cast<uint32_t>(g.regions.size());
        } else {
            const auto& r=*resource.raster;view.kind=IPROP_RESOURCE_RASTER;
            view.pixel_format=r.format==SurfaceBuffer::Format::A8?IPROP_PIXEL_FORMAT_A8:IPROP_PIXEL_FORMAT_RGBA8;
            view.width=r.raster.width;view.height=r.raster.height;
            view.row_stride=uint64_t{view.width}*(view.pixel_format==IPROP_PIXEL_FORMAT_A8?1u:4u);
            view.byte_count=rasterBytes(r);view.data=rasterData(r);view.scale={r.raster.scaleX,r.raster.scaleY};
        }
        return CallOutcome{};
    });
}

extern "C" iprop_status_ex iprop_result_resource_copy_pixels(const iprop_result* result,uint32_t resource_id,
                                                             uint8_t* out,uint64_t out_capacity,iprop_error_ex* error) {
    const auto resource=Resources(result).get(resource_id);
    const auto* data=resource.raster?rasterData(*resource.raster):nullptr;
    const uint64_t bytes=resource.raster?rasterBytes(*resource.raster):0;
    const std::array protectedSpans{ProtectedSpan{data,capacitySize(bytes)}};
    ErrorContext context{ErrorStage::Arguments};
    return checkedBoundary(error,out,capacitySize(out_capacity),context,[&] {
        if(!result||!resource.raster)return CallOutcome::argument("resource is not a raster");
        if(!out||out_capacity<bytes||out_capacity>std::numeric_limits<std::size_t>::max())
            return CallOutcome::argument("pixel buffer is null or too small/unrepresentable");
        if(bytes)std::memcpy(out,data,static_cast<std::size_t>(bytes));
        return CallOutcome{};
    },protectedSpans);
}

extern "C" iprop_status_ex iprop_result_resource_ring(const iprop_result* result,uint32_t resource_id,
                                                      uint32_t ring_index,iprop_ring_info_ex* out,iprop_error_ex* error) {
    return sized(result,out,error,[&](iprop_ring_info_ex& view) {
        const auto* geometry=Resources(result).get(resource_id).geometry;
        if(!geometry||ring_index>=geometry->rings.size())return CallOutcome::argument("geometry ring index is out of range");
        const auto& ring=geometry->rings[ring_index];
        view.ring_index=ring_index;view.vertex_count=ring.count;view.parent_ring=ring.parent;view.is_hole=ring.hole?1u:0u;
        return CallOutcome{};
    });
}

extern "C" iprop_status_ex iprop_result_resource_ring_copy_vertices(const iprop_result* result,uint32_t resource_id,
                                                                    uint32_t ring_index,iprop_point_value* out,
                                                                    uint64_t out_capacity,iprop_error_ex* error) {
    const auto* g=Resources(result).get(resource_id).geometry;
    const std::array spans{
        ProtectedSpan{g?g->points.data():nullptr,g?g->points.size()*sizeof(Vec2):0},
        ProtectedSpan{g?g->edges.data():nullptr,g?g->edges.size()*sizeof(GeometryEdge):0},
        ProtectedSpan{g?g->uses.data():nullptr,g?g->uses.size()*sizeof(GeometryEdgeUse):0},
        ProtectedSpan{g?g->rings.data():nullptr,g?g->rings.size()*sizeof(GeometryRing):0}};
    ErrorContext context{ErrorStage::Arguments};
    return checkedBoundary(error,out,capacitySize(out_capacity),context,[&] {
        if(!result||!g||ring_index>=g->rings.size())return CallOutcome::argument("geometry ring index is out of range");
        const auto& ring=g->rings[ring_index];const uint64_t bytes=uint64_t{ring.count}*sizeof(iprop_point_value);
        if(!out||out_capacity<bytes||out_capacity>std::numeric_limits<std::size_t>::max())
            return CallOutcome::argument("vertex buffer is null or too small/unrepresentable");
        auto* destination=reinterpret_cast<uint8_t*>(out);
        for(uint32_t i=0;i<ring.count;++i) {
            const auto value=point(g->start(g->uses[ring.begin+i]));
            std::memcpy(destination+std::size_t{i}*sizeof(value),&value,sizeof(value));
        }
        return CallOutcome{};
    },spans);
}

extern "C" iprop_status_ex iprop_result_resource_region(const iprop_result* result,uint32_t resource_id,
                                                        uint32_t region_index,iprop_region_info_ex* out,iprop_error_ex* error) {
    return sized(result,out,error,[&](iprop_region_info_ex& view) {
        const auto* g=Resources(result).get(resource_id).geometry;
        if(!g||region_index>=g->regions.size())return CallOutcome::argument("geometry region index is out of range");
        const auto& region=g->regions[region_index];
        view.region_index=region_index;view.outer_ring=region.outer;view.hole_count=region.holesCount;view.source_id=region.sourceId;
        view.area=region.area;view.center=point(region.center);view.bounds_origin=point(region.boundsOrigin);view.bounds_size=point(region.boundsSize);
        return CallOutcome{};
    });
}

extern "C" iprop_status_ex iprop_result_resource_region_hole(const iprop_result* result,uint32_t resource_id,
                                                             uint32_t region_index,uint32_t hole_index,
                                                             uint32_t* out_ring,iprop_error_ex* error) {
    ErrorContext context{ErrorStage::Arguments};
    return checkedBoundary(error,out_ring,sizeof(*out_ring),context,[&] {
        if(!result||!out_ring)return CallOutcome::argument("result or ring output is null");
        const auto* g=Resources(result).get(resource_id).geometry;
        if(!g||region_index>=g->regions.size())return CallOutcome::argument("geometry region index is out of range");
        const auto& region=g->regions[region_index];
        if(hole_index>=region.holesCount)return CallOutcome::argument("region hole index is out of range");
        *out_ring=g->holes[region.holesBegin+hole_index];return CallOutcome{};
    });
}

extern "C" iprop_status_ex iprop_result_check_legacy(const iprop_result* result,iprop_error_ex* error) {
    ErrorContext context{ErrorStage::Arguments};
    return checkedBoundary(error,nullptr,0,context,[&] {
        if(!result)return CallOutcome::argument("result is null");
        for(const auto& output:result->outputs)if(static_cast<uint32_t>(output.kind)==IPROP_OUTPUT_KIND_PART)
            return CallOutcome::unsupported("part outputs are unsupported by the legacy image/output view");
        return CallOutcome{};
    });
}
