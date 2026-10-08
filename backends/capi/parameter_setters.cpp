#include "iprop.h"

#include <cstddef>
#include <cstring>

namespace {
// Construct only the selected leaf prefix. Generic P.1 owns all validation,
// lookup, alias preflight, error reporting and the nonthrowing replacement.
template<std::size_t Offset, class Value>
void typedValue(iprop_parameter_value_ex& out, iprop_input_type type,
                const Value& value) noexcept {
    static_assert(sizeof(Value) == 4);
    static_assert(Offset + sizeof(Value) <= sizeof(out));
    out.struct_size = static_cast<uint32_t>(Offset + sizeof(Value));
    out.type = type;
    std::memcpy(reinterpret_cast<unsigned char*>(&out) + Offset, &value, sizeof(Value));
}
} // namespace

extern "C" {

iprop_status_ex iprop_parameters_set_f32(iprop_parameters* parameters, uint32_t input_id,
                                        float value, iprop_error_ex* error) {
    iprop_parameter_value_ex descriptor{};
    typedValue<offsetof(iprop_parameter_value_ex, f32)>(descriptor, IPROP_INPUT_F32, value);
    return iprop_parameters_set(parameters, input_id, &descriptor, error);
}

iprop_status_ex iprop_parameters_set_u32(iprop_parameters* parameters, uint32_t input_id,
                                        uint32_t value, iprop_error_ex* error) {
    iprop_parameter_value_ex descriptor{};
    typedValue<offsetof(iprop_parameter_value_ex, u32)>(descriptor, IPROP_INPUT_U32, value);
    return iprop_parameters_set(parameters, input_id, &descriptor, error);
}

iprop_status_ex iprop_parameters_set_i32(iprop_parameters* parameters, uint32_t input_id,
                                        int32_t value, iprop_error_ex* error) {
    iprop_parameter_value_ex descriptor{};
    typedValue<offsetof(iprop_parameter_value_ex, u32)>(descriptor, IPROP_INPUT_U32, value);
    return iprop_parameters_set(parameters, input_id, &descriptor, error);
}

iprop_status_ex iprop_parameters_set_color(iprop_parameters* parameters, uint32_t input_id,
                                          iprop_color_rgba8 value, iprop_error_ex* error) {
    iprop_parameter_value_ex descriptor{};
    typedValue<offsetof(iprop_parameter_value_ex, color)>(descriptor, IPROP_INPUT_COLOR, value);
    return iprop_parameters_set(parameters, input_id, &descriptor, error);
}

iprop_status_ex iprop_parameters_set_f32_named(iprop_parameters* parameters, iprop_utf8_view name,
                                              float value, iprop_error_ex* error) {
    iprop_parameter_value_ex descriptor{};
    typedValue<offsetof(iprop_parameter_value_ex, f32)>(descriptor, IPROP_INPUT_F32, value);
    return iprop_parameters_set_named(parameters, name, &descriptor, error);
}

iprop_status_ex iprop_parameters_set_u32_named(iprop_parameters* parameters, iprop_utf8_view name,
                                              uint32_t value, iprop_error_ex* error) {
    iprop_parameter_value_ex descriptor{};
    typedValue<offsetof(iprop_parameter_value_ex, u32)>(descriptor, IPROP_INPUT_U32, value);
    return iprop_parameters_set_named(parameters, name, &descriptor, error);
}

iprop_status_ex iprop_parameters_set_i32_named(iprop_parameters* parameters, iprop_utf8_view name,
                                              int32_t value, iprop_error_ex* error) {
    iprop_parameter_value_ex descriptor{};
    typedValue<offsetof(iprop_parameter_value_ex, u32)>(descriptor, IPROP_INPUT_U32, value);
    return iprop_parameters_set_named(parameters, name, &descriptor, error);
}

iprop_status_ex iprop_parameters_set_color_named(iprop_parameters* parameters, iprop_utf8_view name,
                                                iprop_color_rgba8 value, iprop_error_ex* error) {
    iprop_parameter_value_ex descriptor{};
    typedValue<offsetof(iprop_parameter_value_ex, color)>(descriptor, IPROP_INPUT_COLOR, value);
    return iprop_parameters_set_named(parameters, name, &descriptor, error);
}

} // extern "C"
