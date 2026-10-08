#include "iprop.h"
#include "CheckedBoundary.hpp"
#include "Handles.hpp"
#include "InputAccess.hpp"
#include "Utf8View.hpp"

#include <cstring>
#include <span>

using iprop::capi::findInput;

namespace {
uint32_t partOutputBegin(const iprop::LoadedFile& file, uint32_t partIndex) noexcept {
    uint32_t begin = static_cast<uint32_t>(file.meta.outputs.size());
    for (uint32_t part = 0; part < partIndex; ++part)
        begin += static_cast<uint32_t>(file.code.parts[part].outputs.size());
    return begin;
}

iprop::capi::CallOutcome partInfo(const iprop_program* program, uint32_t index,
                                iprop_part_info_ex* out) noexcept {
    using namespace iprop::capi;
    if (program == nullptr) return CallOutcome::argument("program is null");
    uint32_t capacity = 0;
    if (!descriptorSize(out, capacity))
        return CallOutcome::argument("part query output is null or smaller than four bytes");
    if (readField<uint32_t>(out, capacity, offsetof(iprop_part_info_ex, flags)) != 0)
        return CallOutcome::argument("part query flags are unsupported");
    if (index >= program->data->code.parts.size()) return CallOutcome::argument("part index is out of range");
    const auto& part = program->data->code.parts[index];
    iprop_part_info_ex info{};
    info.part_index = index;
    info.owner = index + 1;
    info.size_x = part.sizeX; info.size_y = part.sizeY;
    std::memcpy(&info.origin_x, &part.origin.x, sizeof(info.origin_x));
    std::memcpy(&info.origin_y, &part.origin.y, sizeof(info.origin_y));
    info.input_count = static_cast<uint32_t>(part.inputs.size());
    info.output_begin = partOutputBegin(*program->data, index);
    info.output_count = static_cast<uint32_t>(part.outputs.size());
    // Part contains no name field. Function names are a different declaration,
    // and debug/import aliases are never inferred as a stored part name.
#define WRITE(field) writeField(out, capacity, offsetof(iprop_part_info_ex, field), info.field)
    WRITE(part_index); WRITE(owner); WRITE(size_x); WRITE(size_y); WRITE(origin_x); WRITE(origin_y);
    WRITE(input_count); WRITE(output_begin); WRITE(output_count); WRITE(name_present);
#undef WRITE
    writeField(out, capacity, offsetof(iprop_part_info_ex, name) +
        offsetof(iprop_utf8_view, data), info.name.data);
    writeField(out, capacity, offsetof(iprop_part_info_ex, name) +
        offsetof(iprop_utf8_view, byte_count), info.name.byte_count);
    return {};
}

uint32_t outputCount(const iprop_program& program) noexcept {
    static_assert((uint64_t{iprop::kMaxParts} + 1) * UINT16_MAX <= UINT32_MAX);
    uint32_t count = static_cast<uint32_t>(program.data->meta.outputs.size());
    // Parsed counts are u16 per owner, with at most kMaxParts (256) parts.
    // Their sum fits u32 without allocation or an inferred descriptor stride.
    for (const auto& part : program.data->code.parts)
        count += static_cast<uint32_t>(part.outputs.size());
    return count;
}

iprop::capi::CallOutcome outputInfo(const iprop_program* program, uint32_t index,
                                  iprop_output_info_ex* out) noexcept {
    using namespace iprop::capi;
    if (program == nullptr) return CallOutcome::argument("program is null");
    uint32_t capacity = 0;
    if (!descriptorSize(out, capacity))
        return CallOutcome::argument("output query output is null or smaller than four bytes");
    if (readField<uint32_t>(out, capacity, offsetof(iprop_output_info_ex, flags)) != 0)
        return CallOutcome::argument("output query flags are unsupported");

    const iprop::OutputDecl* declaration = nullptr;
    const auto& root = program->data->meta.outputs;
    uint32_t local = index, owner = 0;
    if (local < root.size()) declaration = &root[local];
    else {
        local -= static_cast<uint32_t>(root.size());
        for (const auto& part : program->data->code.parts) {
            ++owner;
            if (local < part.outputs.size()) {
                declaration = &part.outputs[local];
                break;
            }
            local -= static_cast<uint32_t>(part.outputs.size());
        }
    }
    if (declaration == nullptr) return CallOutcome::argument("output index is out of range");

    const auto& name = program->data->pool.strings[declaration->nameIndex];
    iprop_output_info_ex info{};
    info.output_index = index;
    info.owner = owner;
    info.kind = static_cast<iprop_output_kind_ex>(declaration->kind);
    info.is_list = declaration->isList ? 1 : 0;
    info.name.data = name.data();
    info.name.byte_count = static_cast<uint32_t>(name.size());
#define WRITE(field) writeField(out, capacity, offsetof(iprop_output_info_ex, field), info.field)
    WRITE(output_index); WRITE(owner); WRITE(kind); WRITE(is_list);
#undef WRITE
    writeField(out, capacity, offsetof(iprop_output_info_ex, name) +
        offsetof(iprop_utf8_view, data), info.name.data);
    writeField(out, capacity, offsetof(iprop_output_info_ex, name) +
        offsetof(iprop_utf8_view, byte_count), info.name.byte_count);
    return {};
}

iprop::capi::CallOutcome inputInfo(const iprop::LoadedFile& file,
                                 std::span<const iprop::InputDecl> inputs, uint32_t index,
                                 iprop_input_info_ex* out) noexcept {
    using namespace iprop::capi;
    uint32_t capacity = 0;
    if (!descriptorSize(out, capacity))
        return CallOutcome::argument("input query output is null or smaller than four bytes");
    if (readField<uint32_t>(out, capacity, offsetof(iprop_input_info_ex, flags)) != 0)
        return CallOutcome::argument("input query flags are unsupported");
    if (index >= inputs.size())
        return CallOutcome::argument("input index is out of range");

    const auto& input = inputs[index];
    const auto& name = file.pool.strings[input.nameIndex];
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

iprop_status_ex iprop_program_part_count(const iprop_program* program, uint32_t* out_count,
                                        iprop_error_ex* error) {
    iprop::capi::ErrorContext context{iprop::ErrorStage::Arguments};
    return iprop::capi::checkedBoundary(error, out_count, sizeof(uint32_t), context, [&] {
        if (program == nullptr) return iprop::capi::CallOutcome::argument("program is null");
        if (out_count == nullptr) return iprop::capi::CallOutcome::argument("out_count is null");
        *out_count = static_cast<uint32_t>(program->data->code.parts.size());
        return iprop::capi::CallOutcome{};
    });
}

iprop_status_ex iprop_program_part_get(const iprop_program* program, uint32_t part_index,
                                      iprop_part_info_ex* out, iprop_error_ex* error) {
    iprop::capi::ErrorContext context{iprop::ErrorStage::Arguments};
    return iprop::capi::checkedBoundary(error, out, iprop::capi::queryCapacity(out), context,
        [&] { return partInfo(program, part_index, out); });
}

iprop_status_ex iprop_program_part_input_count(const iprop_program* program, uint32_t part_index,
                                              uint32_t* out_count, iprop_error_ex* error) {
    iprop::capi::ErrorContext context{iprop::ErrorStage::Arguments};
    return iprop::capi::checkedBoundary(error, out_count, sizeof(uint32_t), context, [&] {
        if (program == nullptr) return iprop::capi::CallOutcome::argument("program is null");
        if (out_count == nullptr) return iprop::capi::CallOutcome::argument("out_count is null");
        if (part_index >= program->data->code.parts.size())
            return iprop::capi::CallOutcome::argument("part index is out of range");
        *out_count = static_cast<uint32_t>(program->data->code.parts[part_index].inputs.size());
        return iprop::capi::CallOutcome{};
    });
}

iprop_status_ex iprop_program_part_input_get(const iprop_program* program, uint32_t part_index,
                                            uint32_t input_index, iprop_input_info_ex* out,
                                            iprop_error_ex* error) {
    iprop::capi::ErrorContext context{iprop::ErrorStage::Arguments};
    return iprop::capi::checkedBoundary(error, out, iprop::capi::queryCapacity(out), context, [&] {
        if (program == nullptr) return iprop::capi::CallOutcome::argument("program is null");
        if (part_index >= program->data->code.parts.size())
            return iprop::capi::CallOutcome::argument("part index is out of range");
        return inputInfo(*program->data, program->data->code.parts[part_index].inputs, input_index, out);
    });
}

iprop_status_ex iprop_program_part_input_find(const iprop_program* program, uint32_t part_index,
                                             iprop_utf8_view name, uint32_t* out_index,
                                             iprop_error_ex* error) {
    iprop::capi::ErrorContext context{iprop::ErrorStage::Arguments};
    return iprop::capi::checkedBoundary(error, out_index, sizeof(uint32_t), context, [&] {
        if (program == nullptr) return iprop::capi::CallOutcome::argument("program is null");
        if (part_index >= program->data->code.parts.size())
            return iprop::capi::CallOutcome::argument("part index is out of range");
        return findInput(*program->data, program->data->code.parts[part_index].inputs, name, out_index);
    });
}

iprop_status_ex iprop_program_output_count(const iprop_program* program, uint32_t* out_count,
                                          iprop_error_ex* error) {
    iprop::capi::ErrorContext context{iprop::ErrorStage::Arguments};
    return iprop::capi::checkedBoundary(error, out_count, sizeof(uint32_t), context, [&] {
        if (program == nullptr) return iprop::capi::CallOutcome::argument("program is null");
        if (out_count == nullptr) return iprop::capi::CallOutcome::argument("out_count is null");
        *out_count = outputCount(*program);
        return iprop::capi::CallOutcome{};
    });
}

iprop_status_ex iprop_program_output_get(const iprop_program* program, uint32_t output_index,
                                        iprop_output_info_ex* out, iprop_error_ex* error) {
    iprop::capi::ErrorContext context{iprop::ErrorStage::Arguments};
    return iprop::capi::checkedBoundary(error, out, iprop::capi::queryCapacity(out), context,
        [&] { return outputInfo(program, output_index, out); });
}

iprop_status_ex iprop_program_output_find(const iprop_program* program, uint32_t owner,
                                         iprop_utf8_view name, uint32_t* out_index,
                                         iprop_error_ex* error) {
    iprop::capi::ErrorContext context{iprop::ErrorStage::Arguments};
    return iprop::capi::checkedBoundary(error, out_index, sizeof(uint32_t), context, [&] {
        using iprop::capi::CallOutcome;
        if (program == nullptr) return CallOutcome::argument("program is null");
        if (out_index == nullptr) return CallOutcome::argument("out_index is null");
        if (owner > program->data->code.parts.size())
            return CallOutcome::argument("output owner is out of range");
        std::string_view sought;
        if (!iprop::capi::readUtf8View(name, sought))
            return CallOutcome::argument("output name is not a valid UTF-8 view");
        const auto& root = program->data->meta.outputs;
        const auto& declarations = owner == 0 ? root : program->data->code.parts[owner - 1].outputs;
        const uint32_t base = owner == 0 ? 0 : partOutputBegin(*program->data, owner - 1);
        for (std::size_t index = 0; index < declarations.size(); ++index) {
            if (program->data->pool.strings[declarations[index].nameIndex] == sought) {
                *out_index = base + static_cast<uint32_t>(index);
                return CallOutcome{};
            }
        }
        return CallOutcome::argument("output name was not found for this owner");
    });
}

iprop_status_ex iprop_program_input_count(const iprop_program* program, uint32_t* out_count,
                                         iprop_error_ex* error) {
    iprop::capi::ErrorContext context{iprop::ErrorStage::Arguments};
    return iprop::capi::checkedBoundary(error, out_count, sizeof(uint32_t), context, [&] {
        if (program == nullptr) return iprop::capi::CallOutcome::argument("program is null");
        if (out_count == nullptr) return iprop::capi::CallOutcome::argument("out_count is null");
        *out_count = static_cast<uint32_t>(program->data->meta.inputs.size());
        return iprop::capi::CallOutcome{};
    });
}

iprop_status_ex iprop_program_input_get(const iprop_program* program, uint32_t input_index,
                                       iprop_input_info_ex* out, iprop_error_ex* error) {
    iprop::capi::ErrorContext context{iprop::ErrorStage::Arguments};
    return iprop::capi::checkedBoundary(error, out, iprop::capi::queryCapacity(out), context,
        [&] {
            if (program == nullptr) return iprop::capi::CallOutcome::argument("program is null");
            return inputInfo(*program->data, program->data->meta.inputs, input_index, out);
        });
}

iprop_status_ex iprop_program_input_find(const iprop_program* program, iprop_utf8_view name,
                                        uint32_t* out_index, iprop_error_ex* error) {
    iprop::capi::ErrorContext context{iprop::ErrorStage::Arguments};
    return iprop::capi::checkedBoundary(error, out_index, sizeof(uint32_t), context, [&] {
        if (program == nullptr) return iprop::capi::CallOutcome::argument("program is null");
        return findInput(*program->data, program->data->meta.inputs, name, out_index);
    });
}

} // extern "C"
