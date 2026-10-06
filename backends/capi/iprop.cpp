// C adapters share one decoder/validator/VM core and retain distinct policies.
#include "iprop.h"
#include "CheckedBoundary.hpp"
#include "LegacyBoundary.hpp"
#include "Operations.hpp"

#include <limits>
#include <memory>

extern "C" {

iprop_status iprop_program_load(const uint8_t* bytes, size_t length,
                                iprop_program** out_program,
                                char* err, size_t err_size) {
    return iprop::capi::legacyBoundary(iprop::capi::LegacyOperation::Load, err, err_size, [&] {
        if (bytes == nullptr || out_program == nullptr)
            return iprop::capi::CallOutcome::argument();
        auto program = std::make_unique<iprop_program>();
        iprop::capi::ErrorContext context;
        iprop::capi::loadProgram(*program, {bytes, length}, context);
        *out_program = program.release();
        return iprop::capi::CallOutcome{};
    });
}

iprop_status_ex iprop_program_load_ex(const uint8_t* bytes, uint64_t byte_count,
                                     iprop_program** out_program, iprop_error_ex* error) {
    // This owner survives the guarded callable and every diagnostic catch.
    std::unique_ptr<iprop_program> program;
    iprop::capi::ErrorContext context{iprop::ErrorStage::Load};
    return iprop::capi::checkedBoundary(error, out_program, sizeof(iprop_program*), context, [&] {
        if (bytes == nullptr) return iprop::capi::CallOutcome::argument("bytes is null");
        if (out_program == nullptr) return iprop::capi::CallOutcome::argument("out_program is null");
        if (byte_count > std::numeric_limits<std::size_t>::max())
            return iprop::capi::CallOutcome::argument("byte_count cannot be represented on this platform");
        program = std::make_unique<iprop_program>();
        iprop::capi::loadProgram(*program, {bytes, static_cast<std::size_t>(byte_count)}, context);
        *out_program = program.release();
        return iprop::capi::CallOutcome{};
    });
}

void iprop_program_free(iprop_program* program) { delete program; }

void iprop_program_meta(const iprop_program* program, iprop_meta* out) {
    if (program != nullptr && out != nullptr) *out = iprop::capi::legacyMeta(*program);
}

iprop_status_ex iprop_program_query(const iprop_program* program, iprop_program_info_ex* out,
                                   iprop_error_ex* error) {
    iprop::capi::ErrorContext context{iprop::ErrorStage::Arguments};
    return iprop::capi::checkedBoundary(error, out, iprop::capi::queryCapacity(out), context, [&] {
        if (program == nullptr) return iprop::capi::CallOutcome::argument("program is null");
        uint32_t size = 0;
        if (!iprop::capi::descriptorSize(out, size))
            return iprop::capi::CallOutcome::argument("program query output is null or smaller than four bytes");
        const auto info = iprop::capi::programInfo(*program);
#define WRITE(field) iprop::capi::writeField(out, size, offsetof(iprop_program_info_ex, field), info.field)
        WRITE(size_x); WRITE(size_y); WRITE(origin_x); WRITE(origin_y);
        WRITE(variants); WRITE(archetypes); WRITE(fuel);
#undef WRITE
        return iprop::capi::CallOutcome{};
    });
}

iprop_status iprop_generate(const iprop_program* program,
                            const iprop_generate_options* options,
                            iprop_result** out_result,
                            char* err, size_t err_size) {
    return iprop::capi::legacyBoundary(iprop::capi::LegacyOperation::Generate, err, err_size, [&] {
        if (program == nullptr || options == nullptr || out_result == nullptr)
            return iprop::capi::CallOutcome::argument();
        if (options->uniform_count != 0 && options->uniforms == nullptr)
            return iprop::capi::CallOutcome::argument();
        iprop::GenerationInputs inputs;
        const auto read = iprop::capi::legacyInputs(*options, inputs);
        if (!read.succeeded) return read;
        auto result = std::make_unique<iprop_result>();
        iprop::capi::ErrorContext context;
        iprop::capi::generateProgram(*program, inputs, *result, context);
        *out_result = result.release();
        return iprop::capi::CallOutcome{};
    });
}

void iprop_result_free(iprop_result* result) { delete result; }

void iprop_result_image(const iprop_result* result, iprop_image* out) {
    if (result == nullptr || out == nullptr) return;
    const auto image = iprop::capi::resultImage(*result);
    out->width = image.width;
    out->height = image.height;
    out->pixels = image.pixels;
    out->output_count = image.output_count;
    out->outputs = image.outputs;
}

} // extern "C"
