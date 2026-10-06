#include "iprop.h"
#include "CheckedBoundary.hpp"
#include "Handles.hpp"
#include "Utf8View.hpp"

#include <cstring>

namespace {
iprop::capi::CallOutcome inputInfo(const iprop_program* program, uint32_t index,
                                 iprop_input_info_ex* out) noexcept {
    using namespace iprop::capi;
    if (program == nullptr) return CallOutcome::argument("program is null");
    uint32_t capacity = 0;
    if (!descriptorSize(out, capacity))
        return CallOutcome::argument("input query output is null or smaller than four bytes");
    if (readField<uint32_t>(out, capacity, offsetof(iprop_input_info_ex, flags)) != 0)
        return CallOutcome::argument("input query flags are unsupported");
    if (index >= program->file.meta.inputs.size())
        return CallOutcome::argument("input index is out of range");

    const auto& input = program->file.meta.inputs[index];
    const auto& name = program->file.pool.strings[input.nameIndex];
    iprop_input_info_ex info{};
    info.input_index = index;
    info.type = static_cast<iprop_input_type>(input.type);
    // The decoder reads string lengths as u32 and input counts as u16.
    info.name.data = name.data();
    info.name.byte_count = static_cast<uint32_t>(name.size());
    switch (input.type) {
    case iprop::InputType::Color:
        info.color_default = {input.colorDefault.r, input.colorDefault.g,
                              input.colorDefault.b, input.colorDefault.a};
        break;
    case iprop::InputType::F32:
        // Copy representation, without arithmetic or float canonicalization.
        std::memcpy(&info.f32_default, &input.f32Default, sizeof(info.f32_default));
        break;
    case iprop::InputType::U32:
        info.u32_default = input.u32Default;
        break;
    }

#define WRITE(field) writeField(out, capacity, offsetof(iprop_input_info_ex, field), info.field)
    WRITE(input_index); WRITE(type); WRITE(color_default); WRITE(f32_default); WRITE(u32_default);
#undef WRITE
    writeField(out, capacity, offsetof(iprop_input_info_ex, name) +
        offsetof(iprop_utf8_view, data), info.name.data);
    writeField(out, capacity, offsetof(iprop_input_info_ex, name) +
        offsetof(iprop_utf8_view, byte_count), info.name.byte_count);
    return {};
}
} // namespace

extern "C" {

iprop_status_ex iprop_program_input_count(const iprop_program* program, uint32_t* out_count,
                                         iprop_error_ex* error) {
    iprop::capi::ErrorContext context{iprop::ErrorStage::Arguments};
    return iprop::capi::checkedBoundary(error, out_count, sizeof(uint32_t), context, [&] {
        if (program == nullptr) return iprop::capi::CallOutcome::argument("program is null");
        if (out_count == nullptr) return iprop::capi::CallOutcome::argument("out_count is null");
        *out_count = static_cast<uint32_t>(program->file.meta.inputs.size());
        return iprop::capi::CallOutcome{};
    });
}

iprop_status_ex iprop_program_input_get(const iprop_program* program, uint32_t input_index,
                                       iprop_input_info_ex* out, iprop_error_ex* error) {
    iprop::capi::ErrorContext context{iprop::ErrorStage::Arguments};
    return iprop::capi::checkedBoundary(error, out, iprop::capi::queryCapacity(out), context,
        [&] { return inputInfo(program, input_index, out); });
}

iprop_status_ex iprop_program_input_find(const iprop_program* program, iprop_utf8_view name,
                                        uint32_t* out_index, iprop_error_ex* error) {
    iprop::capi::ErrorContext context{iprop::ErrorStage::Arguments};
    return iprop::capi::checkedBoundary(error, out_index, sizeof(uint32_t), context, [&] {
        if (program == nullptr) return iprop::capi::CallOutcome::argument("program is null");
        if (out_index == nullptr) return iprop::capi::CallOutcome::argument("out_index is null");
        std::string_view sought;
        if (!iprop::capi::readUtf8View(name, sought))
            return iprop::capi::CallOutcome::argument("input name is not a valid UTF-8 view");
        const auto& inputs = program->file.meta.inputs;
        for (std::size_t index = 0; index < inputs.size(); ++index) {
            if (program->file.pool.strings[inputs[index].nameIndex] == sought) {
                *out_index = static_cast<uint32_t>(index);
                return iprop::capi::CallOutcome{};
            }
        }
        return iprop::capi::CallOutcome::argument("input name was not found");
    });
}

} // extern "C"
