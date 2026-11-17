// Helpers shared by the classes of the extension: names, structured errors,
// input values and execution limits, each defined once.

#pragma once

#include "iprop_godot.hpp"

#include <godot_cpp/variant/utility_functions.hpp>

#include <cstdint>
#include <cstring>
#include <limits>

namespace iprop_godot {

using namespace godot;

// Godot's UTF-8 parser stops at embedded NUL even with an explicit length.
// Decode bounded non-NUL segments with its parser, then store each U+0000
// directly in the length-counted String. No UTF-8 decoder is duplicated here.
inline bool decodeName(iprop_utf8_view view, String& name) {
    if (view.byte_count == 0) return true;
    if (std::memchr(view.data, 0, view.byte_count) == nullptr) {
        return name.parse_utf8(view.data, view.byte_count) == OK;
    }
    if (name.resize(static_cast<int64_t>(view.byte_count) + 1) != OK) return false;
    char32_t* target = name.ptrw();
    int64_t used = 0;
    uint32_t begin = 0;
    while (begin < view.byte_count) {
        const char* zero = static_cast<const char*>(std::memchr(view.data + begin, 0, view.byte_count - begin));
        const auto end = zero == nullptr ? view.byte_count : static_cast<uint32_t>(zero - view.data);
        if (end != begin) {
            String segment;
            if (segment.parse_utf8(view.data + begin, end - begin) != OK) return false;
            std::memcpy(target + used, segment.ptr(), static_cast<size_t>(segment.length()) * sizeof(char32_t));
            used += segment.length();
        }
        if (zero == nullptr) break;
        target[used++] = 0;
        begin = end + 1;
    }
    target[used] = 0; // String::resize requires the caller to terminate storage.
    return name.resize(used + 1) == OK;
}

// A structured C API error with its own message buffer.
class Failure {
public:
    Failure() {
        iprop_error_ex_init(&error_, sizeof(error_));
        error_.message_buffer = message_;
        error_.message_capacity = sizeof(message_);
    }
    Failure(const Failure&) = delete;
    Failure& operator=(const Failure&) = delete;

    iprop_error_ex* get() { return &error_; }
    Dictionary details() const {
        Dictionary result;
        result["status"] = static_cast<int64_t>(error_.status);
        result["reason"] = static_cast<int64_t>(error_.reason);
        result["stage"] = static_cast<int64_t>(error_.stage);
        result["location_flags"] = static_cast<int64_t>(error_.location_flags);
        result["pc"] = static_cast<int64_t>(error_.pc);
        result["message"] = String::utf8(message_);
        return result;
    }

    // Prints "iprop: <operation> failed: <message> (reason N)".
    void print(const char* operation, const char* fallback = "unknown error") const {
        UtilityFunctions::printerr("iprop: ", operation, " failed: ",
            String::utf8(message_[0] != '\0' ? message_ : fallback),
            " (reason ", static_cast<int64_t>(error_.reason), ")");
    }

private:
    iprop_error_ex error_;
    char message_[512] = {};
};

inline const char* typeName(iprop_input_type type) {
    switch (type) {
    case IPROP_INPUT_COLOR: return "color";
    case IPROP_INPUT_F32: return "f32";
    default: return "u32";
    }
}

inline Variant inputValue(iprop_input_type type, iprop_color_rgba8 color, float f32, uint32_t u32) {
    switch (type) {
    case IPROP_INPUT_COLOR: return Color::from_rgba8(color.r, color.g, color.b, color.a);
    case IPROP_INPUT_F32: return f32;
    default: return static_cast<int64_t>(u32);
    }
}

// Sets one input of a parameter set by name: a Color for a color input, a
// number for f32, an int in -2^31..2^32-1 for u32 (negative values keep their
// i32 bits). Returns OK, ERR_UNCONFIGURED without a set, or
// ERR_INVALID_PARAMETER with the error printed.
inline Error setNamedInput(iprop_parameters* parameters, const String& name, const Variant& value) {
    if (parameters == nullptr) {
        UtilityFunctions::printerr("iprop: set_input before load_file");
        return ERR_UNCONFIGURED;
    }
    const CharString bytes = name.utf8();
    const iprop_utf8_view view{bytes.get_data(), static_cast<uint32_t>(bytes.length())};
    Failure failure;
    iprop_parameter_value_ex current;
    if (iprop_parameter_value_ex_init(&current, sizeof(current)) != IPROP_EX_OK ||
        iprop_parameters_get_named(parameters, view, &current, failure.get()) != IPROP_EX_OK) {
        failure.print("set_input");
        return ERR_INVALID_PARAMETER;
    }
    // The declared type picks the setter; u32 inputs also take negative i32 bits.
    const Variant::Type kind = value.get_type();
    iprop_status_ex status;
    if (current.type == IPROP_INPUT_COLOR && kind == Variant::COLOR) {
        const Color color = value;
        const iprop_color_rgba8 rgba{static_cast<uint8_t>(color.get_r8()), static_cast<uint8_t>(color.get_g8()),
                                     static_cast<uint8_t>(color.get_b8()), static_cast<uint8_t>(color.get_a8())};
        status = iprop_parameters_set_color_named(parameters, view, rgba, failure.get());
    } else if (current.type == IPROP_INPUT_F32 && (kind == Variant::FLOAT || kind == Variant::INT)) {
        status = iprop_parameters_set_f32_named(parameters, view, static_cast<float>(static_cast<double>(value)),
                                                failure.get());
    } else if (const int64_t number = kind == Variant::INT ? static_cast<int64_t>(value) : -1;
               current.type == IPROP_INPUT_U32 && kind == Variant::INT &&
               number >= std::numeric_limits<int32_t>::min() && number <= std::numeric_limits<uint32_t>::max()) {
        status = number < 0
            ? iprop_parameters_set_i32_named(parameters, view, static_cast<int32_t>(number), failure.get())
            : iprop_parameters_set_u32_named(parameters, view, static_cast<uint32_t>(number), failure.get());
    } else {
        const char* expected = current.type == IPROP_INPUT_COLOR ? "a Color"
                             : current.type == IPROP_INPUT_F32   ? "a number"
                                                                 : "an int in -2147483648..4294967295";
        UtilityFunctions::printerr("iprop: set_input failed: ", name, " takes ", expected);
        return ERR_INVALID_PARAMETER;
    }
    if (status != IPROP_EX_OK) {
        failure.print("set_input");
        return ERR_INVALID_PARAMETER;
    }
    return OK;
}

// The `limits` dictionary of a generation: work_limit and timeout_ns as
// nonnegative ints, cancellation as an IpropCancellation, which the caller
// keeps referenced while the generation runs. Returns the problem, or null.
inline const char* readLimits(const Dictionary& limits, iprop_execution_limits_ex& execution,
                              Ref<IpropCancellation>& cancellation) {
    iprop_execution_limits_ex_init(&execution, sizeof(execution));
    const Array keys = limits.keys();
    for (int64_t i = 0; i < keys.size(); ++i) {
        const Variant key = keys[i];
        if (key.get_type() != Variant::STRING && key.get_type() != Variant::STRING_NAME)
            return "execution limit keys must be strings";
        const String name = key;
        const Variant value = limits[key];
        if (name == "work_limit" || name == "timeout_ns") {
            if (value.get_type() != Variant::INT || static_cast<int64_t>(value) < 0)
                return "execution counters must be nonnegative int values";
            const auto count = static_cast<uint64_t>(static_cast<int64_t>(value));
            if (name == "work_limit") execution.work_limit = count;
            else execution.timeout_ns = count;
        } else if (name == "cancellation") {
            if (value.get_type() != Variant::OBJECT)
                return "cancellation requires IpropCancellation";
            cancellation = Ref<IpropCancellation>(Object::cast_to<IpropCancellation>(static_cast<Object*>(value)));
            if (cancellation.is_null() || !cancellation->is_valid())
                return "cancellation requires a valid IpropCancellation";
            execution.cancellation = cancellation->handle();
        } else return "unknown execution limit";
    }
    return nullptr;
}

} // namespace iprop_godot
