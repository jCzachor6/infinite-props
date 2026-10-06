#include "iprop.h"
#include "CheckedBoundary.hpp"
#include "Handles.hpp"
#include "Operations.hpp"

extern "C" iprop_status_ex iprop_image_view_ex_init(iprop_image_view_ex* out, uint32_t capacity) {
    if (out == nullptr || capacity < sizeof(uint32_t)) return IPROP_EX_E_ARGUMENT;
    iprop::capi::writeField(out, capacity, 0, capacity);
    const iprop_image_view_ex defaults{};
#define ZERO(field) iprop::capi::writeField(out, capacity, offsetof(iprop_image_view_ex, field), defaults.field)
    ZERO(flags); ZERO(width); ZERO(height); ZERO(byte_count); ZERO(row_stride);
    ZERO(pixel_format); ZERO(alpha_mode); ZERO(origin); ZERO(pixels);
#undef ZERO
    return IPROP_EX_OK;
}

extern "C" iprop_status_ex iprop_result_query_image(const iprop_result* result,
                                                    iprop_image_view_ex* out,
                                                    iprop_error_ex* error) {
    using namespace iprop::capi;
    ErrorContext context{iprop::ErrorStage::Arguments};
    return checkedBoundary(error, out, queryCapacity(out), context, [&] {
        if (result == nullptr) return CallOutcome::argument("result is null");
        uint32_t capacity = 0;
        if (!descriptorSize(out, capacity))
            return CallOutcome::argument("image view output is null or smaller than four bytes");
        if (readField<uint32_t>(out, capacity, offsetof(iprop_image_view_ex, flags)) != 0)
            return CallOutcome::argument("image view flags are unsupported");
        const auto legacy = resultImage(*result);
        static_assert(sizeof(iprop::Color) == 4);
        static_assert(offsetof(iprop::Color, r) == 0 && offsetof(iprop::Color, g) == 1 &&
                      offsetof(iprop::Color, b) == 2 && offsetof(iprop::Color, a) == 3);
        iprop_image_view_ex view{};
        view.width = legacy.width;
        view.height = legacy.height;
        view.byte_count = uint64_t{result->image.pixels.size()} * sizeof(iprop::Color);
        view.row_stride = uint64_t{legacy.width} * sizeof(iprop::Color);
        view.pixel_format = IPROP_PIXEL_FORMAT_RGBA8;
        view.alpha_mode = IPROP_IMAGE_ALPHA_LEGACY_DESTINATION;
        view.origin = IPROP_IMAGE_ORIGIN_TOP_LEFT;
        view.pixels = legacy.pixels;
#define WRITE(field) writeField(out, capacity, offsetof(iprop_image_view_ex, field), view.field)
        WRITE(width); WRITE(height); WRITE(byte_count); WRITE(row_stride);
        WRITE(pixel_format); WRITE(alpha_mode); WRITE(origin); WRITE(pixels);
#undef WRITE
        return CallOutcome{};
    });
}

extern "C" iprop_status_ex iprop_result_query_stats(const iprop_result* result,
                                                    iprop_result_stats_ex* out,
                                                    iprop_error_ex* error) {
    using namespace iprop::capi;
    ErrorContext context{iprop::ErrorStage::Arguments};
    return checkedBoundary(error, out, queryCapacity(out), context, [&] {
        if (result == nullptr) return CallOutcome::argument("result is null");
        uint32_t capacity = 0;
        if (!descriptorSize(out, capacity))
            return CallOutcome::argument("result statistics output is null or smaller than four bytes");
        if (readField<uint32_t>(out, capacity, offsetof(iprop_result_stats_ex, flags)) != 0)
            return CallOutcome::argument("result statistics flags are unsupported");
        iprop_result_stats_ex stats{};
        stats.fuel_budget = result->fuelBudget;
        stats.fuel_remaining = result->fuelRemaining;
        stats.fuel_used = result->fuelBudget - result->fuelRemaining;
        static_assert(sizeof(iprop::Color) == 4);
        stats.image_byte_count = uint64_t{result->image.pixels.size()} * sizeof(iprop::Color);
        for (const auto& output : result->outputs) stats.output_element_count += output.count;
#define WRITE(field) writeField(out, capacity, offsetof(iprop_result_stats_ex, field), stats.field)
        WRITE(fuel_budget); WRITE(fuel_remaining); WRITE(fuel_used);
        WRITE(image_byte_count); WRITE(output_element_count);
#undef WRITE
        return CallOutcome{};
    });
}
