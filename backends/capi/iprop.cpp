// The C API over the iprop runtime: owns the handles, converts between
// the C structs and the C++ types, and maps the C++ exceptions onto the
// status codes. The C++ runtime headers are not part of the C ABI.

#include "iprop.h"
#include "ErrorSink.hpp"

#include "iprop/VM.hpp"
#include "iprop/Validator.hpp"

#include <memory>
#include <new>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// The definitions behind the opaque handles of iprop.h. The struct tags
// must match the header's forward declarations (and live in the global
// namespace) so the pointers are the same types across the boundary.
struct iprop_program {
    iprop::LoadedFile file;
};

struct iprop_result {
    iprop::GenerationResult generation;
    // The C-visible table: the names and the packed float values are
    // owned here so the borrowed pointers stay valid until the free.
    std::vector<std::string> names;
    std::vector<float> packed;
    std::vector<iprop_output> outputs;
};

namespace {

// Copies the message into the caller's buffer, truncated and
// NUL-terminated.
void copyError(std::string_view message, char* err, size_t errSize) noexcept {
    // what() converts to a borrowed view, never an allocating std::string.
    // Preserve the old byte truncation, untouched argument/OOM/success buffers
    // and status policies while making catch handlers safe under allocation failure.
    iprop::capi::copyMessage(message, {err, errSize});
}

// Rebuilds the C output table from the C++ generation result. The packed
// floats are reserved up front so the per-slot pointers never dangle.
void fillResult(iprop_result* result, const iprop::LoadedFile& file) {
    result->names.clear();
    result->packed.clear();
    result->outputs.clear();

    const auto& outputs = result->generation.outputs;
    result->names.reserve(outputs.size());
    for (const auto& output : outputs) {
        result->names.push_back(output.name);
    }

    size_t floats = 0;
    for (const auto& output : outputs) {
        switch (output.kind) {
        case iprop::OutputKind::Point:
            floats += 2 * output.points.size();
            break;
        case iprop::OutputKind::Rect:
            floats += 4 * output.rects.size();
            break;
        case iprop::OutputKind::Circle:
            floats += 3 * output.circles.size();
            break;
        }
    }
    result->packed.reserve(floats);

    result->outputs.reserve(outputs.size());
    size_t ownerEnd = file.meta.outputs.size();
    uint32_t owner = 0;
    for (size_t i = 0; i < outputs.size(); ++i) {
        while (i >= ownerEnd && owner < file.code.parts.size()) {
            ownerEnd += file.code.parts[owner].outputs.size();
            ++owner;
        }
        const auto& output = outputs[i];
        iprop_output entry{};
        entry.owner = owner;
        entry.is_list = output.isList ? 1 : 0;
        entry.name = result->names[i].c_str();
        switch (output.kind) {
        case iprop::OutputKind::Point:
            entry.kind = IPROP_OUTPUT_POINT;
            entry.count = static_cast<uint32_t>(output.points.size());
            for (const auto& point : output.points) {
                result->packed.push_back(point.x);
                result->packed.push_back(point.y);
            }
            break;
        case iprop::OutputKind::Rect:
            entry.kind = IPROP_OUTPUT_RECT;
            entry.count = static_cast<uint32_t>(output.rects.size());
            for (const auto& rect : output.rects) {
                result->packed.push_back(rect.x);
                result->packed.push_back(rect.y);
                result->packed.push_back(rect.w);
                result->packed.push_back(rect.h);
            }
            break;
        case iprop::OutputKind::Circle:
            entry.kind = IPROP_OUTPUT_CIRCLE;
            entry.count = static_cast<uint32_t>(output.circles.size());
            for (const auto& circle : output.circles) {
                result->packed.push_back(circle.x);
                result->packed.push_back(circle.y);
                result->packed.push_back(circle.r);
            }
            break;
        }
        result->outputs.push_back(entry);
    }

    // The values pointers into the (fully reserved) packed array.
    size_t begin = 0;
    for (size_t i = 0; i < outputs.size(); ++i) {
        result->outputs[i].values =
            result->packed.empty() ? nullptr : result->packed.data() + begin;
        begin += result->outputs[i].count *
                 (result->outputs[i].kind == IPROP_OUTPUT_POINT   ? 2
                  : result->outputs[i].kind == IPROP_OUTPUT_RECT ? 4
                                                                 : 3);
    }
}

iprop::UniformValue toUniform(const iprop_uniform& uniform) {
    iprop::UniformValue value;
    switch (uniform.type) {
    case IPROP_UNIFORM_COLOR:
        value.type = iprop::InputType::Color;
        value.color = iprop::Color{uniform.r, uniform.g, uniform.b, uniform.a};
        break;
    case IPROP_UNIFORM_F32:
        value.type = iprop::InputType::F32;
        value.f = uniform.f;
        break;
    case IPROP_UNIFORM_U32:
        value.type = iprop::InputType::U32;
        value.u = uniform.u;
        break;
    }
    return value;
}

} // namespace

extern "C" {

iprop_status iprop_program_load(const uint8_t* bytes, size_t length,
                                iprop_program** out_program,
                                char* err, size_t err_size) {
    if (bytes == nullptr || out_program == nullptr) {
        return IPROP_E_ARGUMENT;
    }
    try {
        auto program = std::make_unique<iprop_program>();
        program->file =
            iprop::loadFile(std::span<const uint8_t>(bytes, length));
        iprop::validate(program->file);
        *out_program = program.release();
        return IPROP_OK;
    } catch (const std::bad_alloc&) {
        return IPROP_E_OOM;
    } catch (const iprop::FormatError& error) {
        copyError(error.what(), err, err_size);
        return IPROP_E_FORMAT;
    } catch (const std::exception& error) {
        copyError(error.what(), err, err_size);
        return IPROP_E_FORMAT;
    } catch (...) {
        copyError("unexpected exception while loading program", err, err_size);
        return IPROP_E_FORMAT;
    }
}

void iprop_program_free(iprop_program* program) { delete program; }

void iprop_program_meta(const iprop_program* program, iprop_meta* out) {
    if (program == nullptr || out == nullptr) {
        return;
    }
    const iprop::AssetMeta& meta = program->file.meta;
    out->size_x = meta.sizeX;
    out->size_y = meta.sizeY;
    out->origin_x = meta.origin.x;
    out->origin_y = meta.origin.y;
    out->variants = meta.variants;
    out->archetypes = meta.archetypes;
    out->fuel = meta.fuel;
}

iprop_status iprop_generate(const iprop_program* program,
                            const iprop_generate_options* options,
                            iprop_result** out_result,
                            char* err, size_t err_size) {
    if (program == nullptr || options == nullptr || out_result == nullptr) {
        return IPROP_E_ARGUMENT;
    }
    if (options->uniform_count != 0 && options->uniforms == nullptr) {
        return IPROP_E_ARGUMENT;
    }
    try {
        iprop::GenerationInputs inputs;
        inputs.seed = options->seed;
        inputs.variant = options->variant;
        inputs.archetype = options->archetype;
        inputs.sizeX = options->size_x;
        inputs.sizeY = options->size_y;
        for (uint32_t i = 0; i < options->uniform_count; ++i) {
            const iprop_uniform& uniform = options->uniforms[i];
            if (uniform.name == nullptr ||
                uniform.type > IPROP_UNIFORM_U32) {
                return IPROP_E_ARGUMENT;
            }
            inputs.uniforms.emplace(uniform.name, toUniform(uniform));
        }
        auto result = std::make_unique<iprop_result>();
        iprop::VM vm(program->file, inputs);
        vm.run();
        result->generation = vm.result();
        fillResult(result.get(), program->file);
        *out_result = result.release();
        return IPROP_OK;
    } catch (const std::bad_alloc&) {
        return IPROP_E_OOM;
    } catch (const iprop::Trap& error) {
        copyError(error.what(), err, err_size);
        return IPROP_E_TRAP;
    } catch (const iprop::FormatError& error) {
        copyError(error.what(), err, err_size);
        return IPROP_E_FORMAT;
    } catch (const std::exception& error) {
        copyError(error.what(), err, err_size);
        return IPROP_E_TRAP;
    } catch (...) {
        copyError("unexpected exception during generation", err, err_size);
        return IPROP_E_TRAP;
    }
}

void iprop_result_free(iprop_result* result) { delete result; }

void iprop_result_image(const iprop_result* result, iprop_image* out) {
    if (result == nullptr || out == nullptr) {
        return;
    }
    const iprop::Canvas& canvas = result->generation.image;
    out->width = canvas.width;
    out->height = canvas.height;
    out->pixels = canvas.pixels.empty()
                      ? nullptr
                      : reinterpret_cast<const uint8_t*>(canvas.pixels.data());
    out->output_count = static_cast<uint32_t>(result->outputs.size());
    out->outputs = result->outputs.empty() ? nullptr : result->outputs.data();
}

} // extern "C"
