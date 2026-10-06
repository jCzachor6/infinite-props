#include "iprop.h"
#include "CheckedBoundary.hpp"
#include "Handles.hpp"
#include "InputAccess.hpp"

#include "iprop/Constants.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <span>
#include <stdexcept>
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
    if (id >= parameters->values.size())
        return CallOutcome::argument("parameter ID is out of range", iprop::ErrorReason::UnknownParameter);
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

// Offsets of the shared type/value fields in a sized value-carrying descriptor.
struct ValueLayout {
    std::size_t type, color, f32, u32;
};
constexpr ValueLayout kValueLayout{offsetof(iprop_parameter_value_ex, type),
    offsetof(iprop_parameter_value_ex, color), offsetof(iprop_parameter_value_ex, f32),
    offsetof(iprop_parameter_value_ex, u32)};
constexpr ValueLayout kOverrideLayout{offsetof(iprop_parameter_override_ex, type),
    offsetof(iprop_parameter_override_ex, color), offsetof(iprop_parameter_override_ex, f32),
    offsetof(iprop_parameter_override_ex, u32)};

// Exact binary tag and its complete active field; inactive fields are never read.
CallOutcome readTypedValue(const void* input, uint32_t capacity, const ValueLayout& layout,
                           iprop::UniformValue& value) noexcept {
    if (capacity < layout.type + sizeof(iprop_input_type))
        return CallOutcome::argument("parameter value type is incomplete");
    const auto type = readField<iprop_input_type>(input, capacity, layout.type);
    std::size_t offset = 0;
    switch (type) {
    case IPROP_INPUT_COLOR: offset = layout.color; break;
    case IPROP_INPUT_F32: offset = layout.f32; break;
    case IPROP_INPUT_U32: offset = layout.u32; break;
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
        std::memcpy(&value.f, static_cast<const unsigned char*>(input) + offset, sizeof(value.f)); break;
    case IPROP_INPUT_U32: value.u = readField<uint32_t>(input, capacity, offset); break;
    }
    return {};
}

CallOutcome readValue(const iprop_parameter_value_ex* input, iprop::UniformValue& value) noexcept {
    uint32_t capacity = 0;
    if (!descriptorSize(input, capacity))
        return CallOutcome::argument("parameter value is null or smaller than four bytes");
    if (readField<uint32_t>(input, capacity, offsetof(iprop_parameter_value_ex, flags)) != 0)
        return CallOutcome::argument("parameter value flags are unsupported");
    return readTypedValue(input, capacity, kValueLayout, value);
}

CallOutcome setValue(iprop_parameters* parameters, uint32_t id,
                     const iprop_parameter_value_ex* input) noexcept {
    if (parameters == nullptr) return CallOutcome::argument("parameters is null");
    if (id >= parameters->values.size())
        return CallOutcome::argument("parameter ID is out of range", iprop::ErrorReason::UnknownParameter);
    iprop::UniformValue value;
    const auto read = readValue(input, value);
    if (!read.succeeded) return read;
    if (value.type != parameters->data->meta.inputs[id].type)
        return CallOutcome::argument("parameter value type does not match declaration", iprop::ErrorReason::UniformType);
    // Validation is complete. The sole mutation cannot throw or allocate.
    std::memcpy(&parameters->values[id], &value, sizeof(value));
    return {};
}

constexpr std::size_t kOverrideNameData = offsetof(iprop_parameter_override_ex, name) +
    offsetof(iprop_utf8_view, data);
constexpr std::size_t kOverrideNameCount = offsetof(iprop_parameter_override_ex, name) +
    offsetof(iprop_utf8_view, byte_count);

// Descriptor shape of one list element: size within the stride, no flags, a
// complete known target with its selector, then the typed active value.
CallOutcome readOverrideShape(const unsigned char* element, uint32_t stride, uint32_t& size,
                              iprop::UniformValue& value) noexcept {
    if (!descriptorSize(element, size))
        return CallOutcome::argument("override struct_size is smaller than four bytes");
    if (size > stride) return CallOutcome::argument("override struct_size exceeds the list stride");
    if (readField<uint32_t>(element, size, offsetof(iprop_parameter_override_ex, flags)) != 0)
        return CallOutcome::argument("override flags are unsupported");
    if (size < offsetof(iprop_parameter_override_ex, target) + sizeof(iprop_parameter_target))
        return CallOutcome::argument("override target is incomplete");
    switch (readField<iprop_parameter_target>(element, size, offsetof(iprop_parameter_override_ex, target))) {
    case IPROP_PARAMETER_TARGET_ID:
        if (size < offsetof(iprop_parameter_override_ex, input_id) + sizeof(uint32_t))
            return CallOutcome::argument("override input_id is incomplete");
        break;
    case IPROP_PARAMETER_TARGET_NAME:
        if (size < kOverrideNameCount + sizeof(uint32_t))
            return CallOutcome::argument("override name is incomplete");
        break;
    default: return CallOutcome::argument("override target is invalid");
    }
    return readTypedValue(element, size, kOverrideLayout, value);
}

// Root-input ID of a well-shaped element; only its selected field is read.
CallOutcome resolveOverride(const iprop_parameters& parameters, const unsigned char* element,
                            uint32_t size, uint32_t& id) noexcept {
    const auto& inputs = parameters.data->meta.inputs;
    if (readField<iprop_parameter_target>(element, size, offsetof(iprop_parameter_override_ex, target)) ==
            IPROP_PARAMETER_TARGET_ID) {
        id = readField<uint32_t>(element, size, offsetof(iprop_parameter_override_ex, input_id));
        return id < inputs.size() ? CallOutcome{} :
            CallOutcome::argument("parameter ID is out of range", iprop::ErrorReason::UnknownParameter);
    }
    const iprop_utf8_view name{readField<const char*>(element, size, kOverrideNameData),
                               readField<uint32_t>(element, size, kOverrideNameCount)};
    return findInput(*parameters.data, inputs, name, &id, iprop::ErrorReason::UnknownParameter);
}

// Every element is validated before the first replacement. A valid list has
// distinct IDs, so a duplicate, unknown or malformed element appears no later
// than position inputs.size(); recorded positions therefore fit uint16_t.
CallOutcome setOverrides(iprop_parameters* parameters, const unsigned char* list, uint32_t count,
                         uint32_t stride, std::span<char> message) {
    if (parameters == nullptr) return CallOutcome::argument("parameters is null");
    if (count == 0) return {};
    if (list == nullptr) return CallOutcome::argument("overrides is null with nonzero count");
    if (stride < sizeof(uint32_t)) return CallOutcome::argument("override stride is smaller than four bytes");
    if (uint64_t{count} * stride > SIZE_MAX) return CallOutcome::argument("override list exceeds the address space");
    static_assert(iprop::kMaxLocals <= UINT16_MAX);
    constexpr uint16_t unlisted = UINT16_MAX;
    std::array<uint16_t, iprop::kMaxLocals> position;
    // Validation bounds root inputs by the entry function's local limit.
    if (parameters->values.size() > position.size())
        throw std::logic_error("parameter set exceeds the validated input limit");
    position.fill(unlisted);
    const auto fail = [&](uint32_t index, const CallOutcome& cause) noexcept {
        const int written = std::snprintf(message.data(), message.size(), "parameter override %u: %.*s",
            static_cast<unsigned>(index), static_cast<int>(cause.message.size()), cause.message.data());
        const auto length = written < 0 ? 0 : std::min(static_cast<std::size_t>(written), message.size() - 1);
        return CallOutcome::argument({message.data(), length}, cause.argumentReason);
    };
    for (uint32_t index = 0; index < count; ++index) {
        const auto* element = list + std::size_t{index} * stride;
        uint32_t size = 0, id = 0;
        iprop::UniformValue value;
        auto outcome = readOverrideShape(element, stride, size, value);
        if (outcome.succeeded) outcome = resolveOverride(*parameters, element, size, id);
        if (!outcome.succeeded) return fail(index, outcome);
        if (position[id] != unlisted)
            return fail(index, CallOutcome::argument("input is already listed by an earlier override",
                                                     iprop::ErrorReason::DuplicateParameter));
        if (value.type != parameters->data->meta.inputs[id].type)
            return fail(index, CallOutcome::argument("parameter value type does not match declaration",
                                                     iprop::ErrorReason::UniformType));
        position[id] = static_cast<uint16_t>(index);
    }
    // Validation is complete. Values are decoded again from list storage,
    // which cannot overlap parameter storage; names are not read again.
    for (std::size_t id = 0; id < parameters->values.size(); ++id) {
        if (position[id] == unlisted) continue;
        const auto* element = list + std::size_t{position[id]} * stride;
        uint32_t size = 0;
        iprop::UniformValue value;
        (void)readOverrideShape(element, stride, size, value);
        std::memcpy(&parameters->values[id], &value, sizeof(value));
    }
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
        const auto found = iprop::capi::findInput(*parameters->data, parameters->data->meta.inputs, name, &id,
                                                  iprop::ErrorReason::UnknownParameter);
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
        const auto found = iprop::capi::findInput(*parameters->data, parameters->data->meta.inputs, name, &id,
                                                  iprop::ErrorReason::UnknownParameter);
        return found.succeeded ? setValue(parameters, id, value) : found;
    }, {value, iprop::capi::queryCapacity(value)}, {name.data, name.byte_count});
}

iprop_status_ex iprop_parameters_set_overrides(iprop_parameters* parameters,
                                              const iprop_parameter_override_ex* overrides,
                                              uint32_t count, uint32_t stride, iprop_error_ex* error) {
    using namespace iprop::capi;
    const auto* list = reinterpret_cast<const unsigned char*>(overrides);
    const bool shaped = count != 0 && list != nullptr && stride >= sizeof(uint32_t) &&
        uint64_t{count} * stride <= SIZE_MAX;
    const std::size_t listBytes = shaped ? std::size_t{count} * stride : 0;
    if (shaped) {
        // Decoding values after the first replacement requires list storage
        // separate from the handle and its values; error text must not land in
        // any name. Both aliases are rejected before error binding.
        if (parameters != nullptr &&
            (storageOverlaps(list, listBytes, parameters, sizeof(*parameters)) ||
             storageOverlaps(list, listBytes, parameters->values.data(),
                             parameters->values.size() * sizeof(iprop::UniformValue))))
            return IPROP_EX_E_ARGUMENT;
        for (uint32_t index = 0; index < count; ++index) {
            const auto* element = list + std::size_t{index} * stride;
            uint32_t size = 0;
            if (!descriptorSize(element, size) || size > stride || size < kOverrideNameCount + sizeof(uint32_t) ||
                readField<iprop_parameter_target>(element, size, offsetof(iprop_parameter_override_ex, target)) !=
                    IPROP_PARAMETER_TARGET_NAME)
                continue;
            if (errorStorageAliases(error, readField<const char*>(element, size, kOverrideNameData),
                                    readField<uint32_t>(element, size, kOverrideNameCount)))
                return IPROP_EX_E_ARGUMENT;
        }
    }
    std::array<char, 160> message{};
    return parameterBoundary(parameters, nullptr, 0, error,
        [&] { return setOverrides(parameters, list, count, stride, message); }, {list, listBytes});
}
} // extern "C"
