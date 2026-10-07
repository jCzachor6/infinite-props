#include "iprop.h"
#include "CheckedBoundary.hpp"
#include "Handles.hpp"
#include "Utf8View.hpp"

#include <cstring>

namespace {
uint32_t stride(iprop_output_kind kind) noexcept {
    switch (kind) {
    case IPROP_OUTPUT_POINT: return 2;
    case IPROP_OUTPUT_RECT: return 4;
    case IPROP_OUTPUT_CIRCLE: return 3;
    }
    return 0; // Only validated, packed outputs reach this adapter.
}

template<class Value, class Copy>
iprop_status_ex typedOutput(const iprop_result* result, uint32_t outputIndex,
                           uint32_t elementIndex, Value* out, uint32_t capacity,
                           iprop_output_kind kind, iprop_error_ex* error, Copy copy) noexcept {
    using namespace iprop::capi;
    ErrorContext context{iprop::ErrorStage::Arguments};
    return checkedBoundary(error, out, capacity, context, [&] {
        if (result == nullptr) return CallOutcome::argument("result is null");
        if (out == nullptr || capacity < sizeof(Value))
            return CallOutcome::argument("typed output buffer is null or too small");
        if (outputIndex >= result->outputs.size())
            return CallOutcome::argument("output index is out of range");
        const auto& output = result->outputs[outputIndex];
        if (output.kind != kind) return CallOutcome::argument("output kind does not match accessor");
        if (elementIndex >= output.count) return CallOutcome::argument("output element index is out of range");
        Value value{};
        copy(value, output.values + std::size_t{elementIndex} * stride(kind));
        // Byte storage permits explicit-capacity host buffers at any alignment.
        std::memcpy(static_cast<void*>(out), &value, sizeof(value));
        return CallOutcome{};
    });
}
} // namespace

extern "C" iprop_status_ex iprop_result_output_view_ex_init(iprop_result_output_view_ex* out,
                                                           uint32_t capacity) {
    using namespace iprop::capi;
    if (out == nullptr || capacity < sizeof(uint32_t)) return IPROP_EX_E_ARGUMENT;
    writeField(out, capacity, 0, capacity);
    const iprop_result_output_view_ex defaults{};
#define ZERO(field) writeField(out, capacity, offsetof(iprop_result_output_view_ex, field), defaults.field)
    ZERO(flags); ZERO(output_index); ZERO(owner); ZERO(kind); ZERO(is_list);
    ZERO(element_count); ZERO(stride_floats); ZERO(values);
#undef ZERO
    writeField(out, capacity, offsetof(iprop_result_output_view_ex, name) +
        offsetof(iprop_utf8_view, data), defaults.name.data);
    writeField(out, capacity, offsetof(iprop_result_output_view_ex, name) +
        offsetof(iprop_utf8_view, byte_count), defaults.name.byte_count);
    return IPROP_EX_OK;
}

extern "C" iprop_status_ex iprop_result_output_count(const iprop_result* result,
                                                    uint32_t* out_count, iprop_error_ex* error) {
    using namespace iprop::capi;
    ErrorContext context{iprop::ErrorStage::Arguments};
    return checkedBoundary(error, out_count, sizeof(uint32_t), context, [&] {
        if (result == nullptr) return CallOutcome::argument("result is null");
        if (out_count == nullptr) return CallOutcome::argument("out_count is null");
        *out_count = static_cast<uint32_t>(result->outputs.size());
        return CallOutcome{};
    });
}

extern "C" iprop_status_ex iprop_result_output_get(const iprop_result* result, uint32_t output_index,
                                                  iprop_result_output_view_ex* out, iprop_error_ex* error) {
    using namespace iprop::capi;
    ErrorContext context{iprop::ErrorStage::Arguments};
    return checkedBoundary(error, out, queryCapacity(out), context, [&] {
        if (result == nullptr) return CallOutcome::argument("result is null");
        uint32_t capacity = 0;
        if (!descriptorSize(out, capacity))
            return CallOutcome::argument("output view is null or smaller than four bytes");
        if (readField<uint32_t>(out, capacity, offsetof(iprop_result_output_view_ex, flags)) != 0)
            return CallOutcome::argument("output view flags are unsupported");
        if (output_index >= result->outputs.size()) return CallOutcome::argument("output index is out of range");
        const auto& output = result->outputs[output_index];
        iprop_result_output_view_ex view{};
        view.output_index = output_index;
        view.owner = output.owner;
        view.kind = static_cast<iprop_output_kind_ex>(output.kind);
        view.is_list = output.is_list;
        view.element_count = output.count;
        view.stride_floats = stride(output.kind);
        view.name.data = output.name;
        view.name.byte_count = static_cast<uint32_t>(result->names[output_index].size());
        view.values = output.values;
#define WRITE(field) writeField(out, capacity, offsetof(iprop_result_output_view_ex, field), view.field)
        WRITE(output_index); WRITE(owner); WRITE(kind); WRITE(is_list);
        WRITE(element_count); WRITE(stride_floats); WRITE(values);
#undef WRITE
        writeField(out, capacity, offsetof(iprop_result_output_view_ex, name) +
            offsetof(iprop_utf8_view, data), view.name.data);
        writeField(out, capacity, offsetof(iprop_result_output_view_ex, name) +
            offsetof(iprop_utf8_view, byte_count), view.name.byte_count);
        return CallOutcome{};
    });
}

extern "C" iprop_status_ex iprop_result_output_find(const iprop_result* result, uint32_t owner,
                                                   iprop_utf8_view name, uint32_t* out_index,
                                                   iprop_error_ex* error) {
    using namespace iprop::capi;
    ErrorContext context{iprop::ErrorStage::Arguments};
    return checkedBoundary(error, out_index, sizeof(uint32_t), context, [&] {
        if (result == nullptr) return CallOutcome::argument("result is null");
        if (out_index == nullptr) return CallOutcome::argument("out_index is null");
        if (owner >= result->ownerCount) return CallOutcome::argument("output owner is out of range");
        std::string_view sought;
        if (!readUtf8View(name, sought)) return CallOutcome::argument("output name is not a valid UTF-8 view");
        for (std::size_t index = 0; index < result->outputs.size(); ++index) {
            if (result->outputs[index].owner == owner && result->names[index] == sought) {
                *out_index = static_cast<uint32_t>(index);
                return CallOutcome{};
            }
        }
        return CallOutcome::argument("output name was not found for this owner");
    });
}

extern "C" iprop_status_ex iprop_result_output_point(const iprop_result* result, uint32_t output_index,
                                                    uint32_t element_index, iprop_point_value* out,
                                                    uint32_t out_capacity, iprop_error_ex* error) {
    static_assert(sizeof(iprop_point_value) == 2 * sizeof(float));
    return typedOutput(result, output_index, element_index, out, out_capacity, IPROP_OUTPUT_POINT, error,
        [](iprop_point_value& value, const float* source) {
            std::memcpy(&value.x, source, sizeof(float));
            std::memcpy(&value.y, source + 1, sizeof(float));
        });
}

extern "C" iprop_status_ex iprop_result_output_rect(const iprop_result* result, uint32_t output_index,
                                                   uint32_t element_index, iprop_rect_value* out,
                                                   uint32_t out_capacity, iprop_error_ex* error) {
    static_assert(sizeof(iprop_rect_value) == 4 * sizeof(float));
    return typedOutput(result, output_index, element_index, out, out_capacity, IPROP_OUTPUT_RECT, error,
        [](iprop_rect_value& value, const float* source) {
            std::memcpy(&value.x, source, sizeof(float));
            std::memcpy(&value.y, source + 1, sizeof(float));
            std::memcpy(&value.w, source + 2, sizeof(float));
            std::memcpy(&value.h, source + 3, sizeof(float));
        });
}

extern "C" iprop_status_ex iprop_result_output_circle(const iprop_result* result, uint32_t output_index,
                                                     uint32_t element_index, iprop_circle_value* out,
                                                     uint32_t out_capacity, iprop_error_ex* error) {
    static_assert(sizeof(iprop_circle_value) == 3 * sizeof(float));
    return typedOutput(result, output_index, element_index, out, out_capacity, IPROP_OUTPUT_CIRCLE, error,
        [](iprop_circle_value& value, const float* source) {
            std::memcpy(&value.x, source, sizeof(float));
            std::memcpy(&value.y, source + 1, sizeof(float));
            std::memcpy(&value.radius, source + 2, sizeof(float));
        });
}
