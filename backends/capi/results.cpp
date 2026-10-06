#include "iprop.h"
#include "CheckedBoundary.hpp"
#include "Handles.hpp"

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
