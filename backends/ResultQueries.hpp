// Engine adapters share checked result reads, without interpreting packed
// geometry or retaining borrowed storage beyond the owning result.
// Helpers consume adapter-owned candidate views/arrays. Optional error storage
// must be separate from those candidates; use the C API directly for arbitrary
// caller-supplied descriptor sizes and alias checks.
#pragma once

#include "iprop.h"

#include <cstdint>
#include <cstring>
#include <limits>
#include <utility>
#include <vector>

namespace iprop::backend {

inline iprop_status_ex queryImage(const iprop_result* result,
                                 iprop_image_view_ex& image,
                                 iprop_error_ex* error = nullptr) {
    iprop_image_view_ex candidate;
    auto status = iprop_image_view_ex_init(&candidate, sizeof(candidate));
    if (status == IPROP_EX_OK) status = iprop_result_query_image(result, &candidate, error);
    if (status == IPROP_EX_OK) image = candidate;
    return status;
}

inline bool rgba8Image(const iprop_image_view_ex& image) {
    const auto stride = static_cast<std::uint64_t>(image.width) * 4;
    if (image.height != 0 && stride > std::numeric_limits<std::uint64_t>::max() / image.height) return false;
    return image.pixel_format == IPROP_PIXEL_FORMAT_RGBA8 &&
           image.alpha_mode == IPROP_IMAGE_ALPHA_LEGACY_DESTINATION &&
           image.origin == IPROP_IMAGE_ORIGIN_TOP_LEFT &&
           image.row_stride == stride && image.byte_count == stride * image.height &&
           (image.byte_count == 0 || image.pixels != nullptr);
}

inline iprop_status_ex queryOutput(const iprop_result* result, std::uint32_t index,
                                  iprop_result_output_view_ex& slot,
                                  iprop_error_ex* error = nullptr) {
    iprop_result_output_view_ex candidate;
    auto status = iprop_result_output_view_ex_init(&candidate, sizeof(candidate));
    if (status == IPROP_EX_OK) status = iprop_result_output_get(result, index, &candidate, error);
    if (status == IPROP_EX_OK) slot = candidate;
    return status;
}

inline iprop_status_ex querySlot(const iprop_result* result, std::uint32_t index,
                                iprop_output_slot_ex& slot, iprop_error_ex* error = nullptr) {
    iprop_output_slot_ex candidate;
    auto status = iprop_output_slot_ex_init(&candidate, sizeof(candidate));
    if (status == IPROP_EX_OK) status = iprop_result_output_slot(result, index, &candidate, error);
    if (status == IPROP_EX_OK) slot = candidate;
    return status;
}

inline iprop_status_ex copyParts(const iprop_result* result, const iprop_output_slot_ex& slot,
                                std::vector<iprop_part_snapshot_ex>& parts, iprop_error_ex* error = nullptr) {
    std::vector<iprop_part_snapshot_ex> candidate(slot.element_count);
    for (std::uint32_t i = 0; i < slot.element_count; ++i) {
        auto& value = candidate[i];
        auto status = iprop_part_snapshot_ex_init(&value, sizeof(value));
        if (status == IPROP_EX_OK) status = iprop_result_output_part(result, slot.output_index, i, &value, error);
        if (status != IPROP_EX_OK) return status;
    }
    parts = std::move(candidate);
    return IPROP_EX_OK;
}

struct ResourceRing {
    iprop_ring_info_ex info{};
    std::vector<iprop_point_value> vertices;
};
struct ResourceRegion {
    iprop_region_info_ex info{};
    std::vector<std::uint32_t> holes;
};
struct ResultResource {
    // data is cleared: the owned pixels below are the only exposed storage.
    iprop_resource_info_ex info{};
    std::vector<std::uint8_t> pixels;
    std::vector<ResourceRing> rings;
    std::vector<ResourceRegion> regions;
};

// One copy per result-local ID, independent of snapshot references. Candidates
// publish together only after every query/copy succeeds. No result pointer is
// retained; all metadata, holes, vertices and pixels survive result_free.
inline iprop_status_ex copyResources(const iprop_result* result, std::vector<ResultResource>& resources,
                                    iprop_error_ex* error = nullptr) {
    std::uint32_t count = 0;
    auto status = iprop_result_resource_count(result, &count, error);
    if (status != IPROP_EX_OK) return status;
    std::vector<ResultResource> candidate(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        auto& resource = candidate[i];
        auto& info = resource.info;
        status = iprop_resource_info_ex_init(&info, sizeof(info));
        if (status == IPROP_EX_OK) status = iprop_result_resource_get(result, i + 1, &info, error);
        if (status != IPROP_EX_OK) return status;
        if (info.byte_count > std::numeric_limits<std::size_t>::max()) return IPROP_EX_E_ARGUMENT;
        resource.pixels.resize(static_cast<std::size_t>(info.byte_count));
        if (info.byte_count != 0) {
            status = iprop_result_resource_copy_pixels(result, i + 1, resource.pixels.data(), info.byte_count, error);
            if (status != IPROP_EX_OK) return status;
        }
        info.data = nullptr;
        resource.rings.resize(info.ring_count);
        for (std::uint32_t r = 0; r < info.ring_count; ++r) {
            auto& ring = resource.rings[r];
            status = iprop_ring_info_ex_init(&ring.info, sizeof(ring.info));
            if (status == IPROP_EX_OK) status = iprop_result_resource_ring(result, i + 1, r, &ring.info, error);
            if (status != IPROP_EX_OK) return status;
            ring.vertices.resize(ring.info.vertex_count);
            if (!ring.vertices.empty()) {
                status = iprop_result_resource_ring_copy_vertices(result, i + 1, r, ring.vertices.data(),
                    static_cast<std::uint64_t>(ring.vertices.size()) * sizeof(iprop_point_value), error);
                if (status != IPROP_EX_OK) return status;
            }
        }
        resource.regions.resize(info.region_count);
        for (std::uint32_t r = 0; r < info.region_count; ++r) {
            auto& region = resource.regions[r];
            status = iprop_region_info_ex_init(&region.info, sizeof(region.info));
            if (status == IPROP_EX_OK) status = iprop_result_resource_region(result, i + 1, r, &region.info, error);
            if (status != IPROP_EX_OK) return status;
            region.holes.resize(region.info.hole_count);
            for (std::uint32_t h = 0; h < region.info.hole_count; ++h) {
                status = iprop_result_resource_region_hole(result, i + 1, r, h, &region.holes[h], error);
                if (status != IPROP_EX_OK) return status;
            }
        }
    }
    resources = std::move(candidate);
    return IPROP_EX_OK;
}

// These are the adapter's published leaf components, not packed-buffer strides.
inline std::uint32_t outputComponents(iprop_output_kind_ex kind) {
    switch (kind) {
    case IPROP_OUTPUT_KIND_POINT: return 2;
    case IPROP_OUTPUT_KIND_RECT: return 4;
    case IPROP_OUTPUT_KIND_CIRCLE: return 3;
    default: return 0;
    }
}

// Copy each named float field as bytes: no float arithmetic (even for NaNs),
// struct-padding assumptions, owner remapping or a second D4 transform.
// The destination has element_count * outputComponents(kind) float capacity.
// Adapters build unpublished local arrays, since a later read can fail.
inline iprop_status_ex copyOutputValues(const iprop_result* result,
                                       const iprop_result_output_view_ex& slot,
                                       float* destination, iprop_error_ex* error = nullptr) {
    const auto components = outputComponents(slot.kind);
    if (components == 0 || (slot.element_count != 0 && destination == nullptr)) {
        return IPROP_EX_E_ARGUMENT;
    }
    for (std::uint32_t i = 0; i < slot.element_count; ++i) {
        float* value = destination + static_cast<std::size_t>(i) * components;
        iprop_status_ex status;
        switch (slot.kind) {
        case IPROP_OUTPUT_KIND_POINT: {
            iprop_point_value point;
            status = iprop_result_output_point(result, slot.output_index, i, &point, sizeof(point), error);
            if (status != IPROP_EX_OK) return status;
            std::memcpy(value, &point.x, sizeof(float));
            std::memcpy(value + 1, &point.y, sizeof(float));
            break;
        }
        case IPROP_OUTPUT_KIND_RECT: {
            iprop_rect_value rect;
            status = iprop_result_output_rect(result, slot.output_index, i, &rect, sizeof(rect), error);
            if (status != IPROP_EX_OK) return status;
            std::memcpy(value, &rect.x, sizeof(float));
            std::memcpy(value + 1, &rect.y, sizeof(float));
            std::memcpy(value + 2, &rect.w, sizeof(float));
            std::memcpy(value + 3, &rect.h, sizeof(float));
            break;
        }
        case IPROP_OUTPUT_KIND_CIRCLE: {
            iprop_circle_value circle;
            status = iprop_result_output_circle(result, slot.output_index, i, &circle, sizeof(circle), error);
            if (status != IPROP_EX_OK) return status;
            std::memcpy(value, &circle.x, sizeof(float));
            std::memcpy(value + 1, &circle.y, sizeof(float));
            std::memcpy(value + 2, &circle.radius, sizeof(float));
            break;
        }
        default: return IPROP_EX_E_ARGUMENT;
        }
    }
    return IPROP_EX_OK;
}

} // namespace iprop::backend
