#include "iprop.h"
#include "CheckedBoundary.hpp"
#include "Handles.hpp"
#include "InputAccess.hpp"

#include <array>
#include <cstring>
#include <memory>
#include <type_traits>

namespace {
using namespace iprop::capi;
static_assert(std::is_trivially_copyable_v<iprop::UniformValue>);

template<class Operation>
iprop_status_ex parameterBoundary(const iprop_parameters* parameters, void* output,
                                  std::size_t outputSize, iprop_error_ex* error,
                                  Operation&& operation, ProtectedSpan value = {},
                                  ProtectedSpan name = {}) noexcept {
    const std::array spans{ProtectedSpan{parameters, parameters ? sizeof(*parameters) : 0},
        ProtectedSpan{parameters ? parameters->values.data() : nullptr,
                      parameters ? parameters->values.size() * sizeof(iprop::UniformValue) : 0},
        value, name};
    ErrorContext context{iprop::ErrorStage::Arguments, parameters ? parameters->data.get() : nullptr};
    return checkedBoundary(error, output, outputSize, context,
                           std::forward<Operation>(operation), spans);
}

void resetValues(iprop_parameters& parameters) noexcept {
    const auto& inputs = parameters.data->meta.inputs;
    for (std::size_t i = 0; i < inputs.size(); ++i) {
        iprop::UniformValue value;
        inputDefault(inputs[i], value);
        std::memcpy(&parameters.values[i], &value, sizeof(value));
    }
}

CallOutcome getValue(const iprop_parameters* parameters, uint32_t id,
                     iprop_parameter_value_ex* out) noexcept {
    if (parameters == nullptr) return CallOutcome::argument("parameters is null");
    uint32_t capacity = 0;
    if (!descriptorSize(out, capacity))
        return CallOutcome::argument("parameter output is null or smaller than four bytes");
    if (readField<uint32_t>(out, capacity, offsetof(iprop_parameter_value_ex, flags)) != 0)
        return CallOutcome::argument("parameter output flags are unsupported");
    if (id >= parameters->values.size()) return CallOutcome::argument("parameter ID is out of range");
    const auto& value = parameters->values[id];
    iprop_parameter_value_ex info{};
    info.type = static_cast<iprop_input_type>(value.type);
    switch (value.type) {
    case iprop::InputType::Color:
        info.color = {value.color.r, value.color.g, value.color.b, value.color.a}; break;
    case iprop::InputType::F32: std::memcpy(&info.f32, &value.f, sizeof(info.f32)); break;
    case iprop::InputType::U32: info.u32 = value.u; break;
    }
#define WRITE(field) writeField(out, capacity, offsetof(iprop_parameter_value_ex, field), info.field)
    WRITE(type); WRITE(color); WRITE(f32); WRITE(u32);
#undef WRITE
    return {};
}

CallOutcome readValue(const iprop_parameter_value_ex* input, iprop::UniformValue& value) noexcept {
    uint32_t capacity = 0;
    if (!descriptorSize(input, capacity))
        return CallOutcome::argument("parameter value is null or smaller than four bytes");
    if (readField<uint32_t>(input, capacity, offsetof(iprop_parameter_value_ex, flags)) != 0)
        return CallOutcome::argument("parameter value flags are unsupported");
    if (capacity < offsetof(iprop_parameter_value_ex, type) + sizeof(iprop_input_type))
        return CallOutcome::argument("parameter value type is incomplete");
    const auto type = readField<iprop_input_type>(input, capacity, offsetof(iprop_parameter_value_ex, type));
    std::size_t offset = 0;
    switch (type) {
    case IPROP_INPUT_COLOR: offset = offsetof(iprop_parameter_value_ex, color); break;
    case IPROP_INPUT_F32: offset = offsetof(iprop_parameter_value_ex, f32); break;
    case IPROP_INPUT_U32: offset = offsetof(iprop_parameter_value_ex, u32); break;
    default: return CallOutcome::argument("parameter value type is invalid");
    }
    if (capacity < offset + 4) return CallOutcome::argument("parameter active value is incomplete");
    value.type = static_cast<iprop::InputType>(type);
    switch (type) {
    case IPROP_INPUT_COLOR: {
        const auto color = readField<iprop_color_rgba8>(input, capacity, offset);
        value.color = {color.r, color.g, color.b, color.a}; break;
    }
    case IPROP_INPUT_F32:
        std::memcpy(&value.f, reinterpret_cast<const unsigned char*>(input) + offset, sizeof(value.f)); break;
    case IPROP_INPUT_U32: value.u = readField<uint32_t>(input, capacity, offset); break;
    }
    return {};
}

CallOutcome setValue(iprop_parameters* parameters, uint32_t id,
                     const iprop_parameter_value_ex* input) noexcept {
    if (parameters == nullptr) return CallOutcome::argument("parameters is null");
    if (id >= parameters->values.size()) return CallOutcome::argument("parameter ID is out of range");
    iprop::UniformValue value;
    const auto read = readValue(input, value);
    if (!read.succeeded) return read;
    if (value.type != parameters->data->meta.inputs[id].type)
        return CallOutcome::argument("parameter value type does not match declaration", iprop::ErrorReason::UniformType);
    // Validation is complete. The sole mutation cannot throw or allocate.
    std::memcpy(&parameters->values[id], &value, sizeof(value));
    return {};
}
} // namespace

extern "C" {

iprop_status_ex iprop_parameters_create(const iprop_program* program, iprop_parameters** out,
                                       iprop_error_ex* error) {
    std::unique_ptr<iprop_parameters> candidate;
    const std::array spans{iprop::capi::ProtectedSpan{program, program ? sizeof(*program) : 0}};
    iprop::capi::ErrorContext context{iprop::ErrorStage::Arguments, program ? program->data.get() : nullptr};
    return iprop::capi::checkedBoundary(error, out, sizeof(iprop_parameters*), context, [&] {
        if (program == nullptr) return iprop::capi::CallOutcome::argument("program is null");
        if (out == nullptr) return iprop::capi::CallOutcome::argument("out_parameters is null");
        candidate = std::make_unique<iprop_parameters>();
        candidate->data = program->data;
        candidate->values.resize(program->data->meta.inputs.size());
        resetValues(*candidate);
        *out = candidate.release();
        return iprop::capi::CallOutcome{};
    }, spans);
}

iprop_status_ex iprop_parameters_clone(const iprop_parameters* parameters, iprop_parameters** out,
                                      iprop_error_ex* error) {
    std::unique_ptr<iprop_parameters> candidate;
    return parameterBoundary(parameters, out, sizeof(iprop_parameters*), error, [&] {
        if (parameters == nullptr) return iprop::capi::CallOutcome::argument("parameters is null");
        if (out == nullptr) return iprop::capi::CallOutcome::argument("out_parameters is null");
        candidate = std::make_unique<iprop_parameters>();
        candidate->data = parameters->data;
        candidate->values.resize(parameters->values.size());
        if (!candidate->values.empty())
            std::memcpy(candidate->values.data(), parameters->values.data(),
                        parameters->values.size() * sizeof(iprop::UniformValue));
        *out = candidate.release();
        return iprop::capi::CallOutcome{};
    });
}

void iprop_parameters_free(iprop_parameters* parameters) { delete parameters; }

iprop_status_ex iprop_parameters_reset(iprop_parameters* parameters, iprop_error_ex* error) {
    return parameterBoundary(parameters, nullptr, 0, error, [&] {
        if (parameters == nullptr) return iprop::capi::CallOutcome::argument("parameters is null");
        resetValues(*parameters);
        return iprop::capi::CallOutcome{};
    });
}

iprop_status_ex iprop_parameters_get(const iprop_parameters* parameters, uint32_t input_id,
                                    iprop_parameter_value_ex* out, iprop_error_ex* error) {
    return parameterBoundary(parameters, out, iprop::capi::queryCapacity(out), error,
                             [&] { return getValue(parameters, input_id, out); });
}

iprop_status_ex iprop_parameters_get_named(const iprop_parameters* parameters, iprop_utf8_view name,
                                          iprop_parameter_value_ex* out, iprop_error_ex* error) {
    return parameterBoundary(parameters, out, iprop::capi::queryCapacity(out), error, [&] {
        if (parameters == nullptr) return iprop::capi::CallOutcome::argument("parameters is null");
        uint32_t id = 0;
        const auto found = iprop::capi::findInput(*parameters->data, parameters->data->meta.inputs, name, &id);
        return found.succeeded ? getValue(parameters, id, out) : found;
    }, {}, {name.data, name.byte_count});
}

iprop_status_ex iprop_parameters_set(iprop_parameters* parameters, uint32_t input_id,
                                    const iprop_parameter_value_ex* value, iprop_error_ex* error) {
    return parameterBoundary(parameters, nullptr, 0, error,
        [&] { return setValue(parameters, input_id, value); }, {value, iprop::capi::queryCapacity(value)});
}

iprop_status_ex iprop_parameters_set_named(iprop_parameters* parameters, iprop_utf8_view name,
                                          const iprop_parameter_value_ex* value, iprop_error_ex* error) {
    return parameterBoundary(parameters, nullptr, 0, error, [&] {
        if (parameters == nullptr) return iprop::capi::CallOutcome::argument("parameters is null");
        uint32_t id = 0;
        const auto found = iprop::capi::findInput(*parameters->data, parameters->data->meta.inputs, name, &id);
        return found.succeeded ? setValue(parameters, id, value) : found;
    }, {value, iprop::capi::queryCapacity(value)}, {name.data, name.byte_count});
}
} // extern "C"
