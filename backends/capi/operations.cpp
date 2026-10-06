#include "Operations.hpp"

#include <cstring>
#include <type_traits>
#include <utility>

namespace iprop::capi {

// Packed float storage remains necessary for the frozen C layout. Reserve
// its full capacity and finalize both tables before publishing any view.
void packResult(iprop_result& result, GenerationResult generation, const LoadedFile& file) {
    result.image = std::move(generation.image);

    auto& outputs = generation.outputs;
    result.names.reserve(outputs.size());
    for (auto& output : outputs) {
        result.names.push_back(std::move(output.name));
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
    result.packed.reserve(floats);

    result.outputs.reserve(outputs.size());
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
        entry.name = result.names[i].c_str();
        switch (output.kind) {
        case iprop::OutputKind::Point:
            entry.kind = IPROP_OUTPUT_POINT;
            entry.count = static_cast<uint32_t>(output.points.size());
            for (const auto& point : output.points) {
                result.packed.push_back(point.x);
                result.packed.push_back(point.y);
            }
            break;
        case iprop::OutputKind::Rect:
            entry.kind = IPROP_OUTPUT_RECT;
            entry.count = static_cast<uint32_t>(output.rects.size());
            for (const auto& rect : output.rects) {
                result.packed.push_back(rect.x);
                result.packed.push_back(rect.y);
                result.packed.push_back(rect.w);
                result.packed.push_back(rect.h);
            }
            break;
        case iprop::OutputKind::Circle:
            entry.kind = IPROP_OUTPUT_CIRCLE;
            entry.count = static_cast<uint32_t>(output.circles.size());
            for (const auto& circle : output.circles) {
                result.packed.push_back(circle.x);
                result.packed.push_back(circle.y);
                result.packed.push_back(circle.r);
            }
            break;
        }
        result.outputs.push_back(entry);
    }

    // The values pointers into the (fully reserved) packed array.
    size_t begin = 0;
    for (size_t i = 0; i < outputs.size(); ++i) {
        result.outputs[i].values =
            result.packed.empty() ? nullptr : result.packed.data() + begin;
        begin += result.outputs[i].count *
                 (result.outputs[i].kind == IPROP_OUTPUT_POINT   ? 2
                  : result.outputs[i].kind == IPROP_OUTPUT_RECT ? 4
                                                                 : 3);
    }
}

namespace {

iprop::UniformValue toUniform(const iprop_uniform& uniform, int type) {
    iprop::UniformValue value;
    switch (type) {
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

void loadProgram(iprop_program& program, std::span<const uint8_t> bytes,
                 ErrorContext& context) {
    context.stage = ErrorStage::Load;
    context.file = nullptr;
    program.file = loadFile(bytes);
    context.file = &program.file;
    context.stage = ErrorStage::Validate;
    validate(program.file);
}

CallOutcome legacyInputs(const iprop_generate_options& options, GenerationInputs& inputs) {
    if (options.uniform_count != 0 && options.uniforms == nullptr)
        return CallOutcome::argument("uniforms is null with nonzero count");
    inputs.seed = options.seed;
    inputs.variant = options.variant;
    inputs.archetype = options.archetype;
    inputs.sizeX = options.size_x;
    inputs.sizeY = options.size_y;
    for (uint32_t i = 0; i < options.uniform_count; ++i) {
        const auto& uniform = options.uniforms[i];
        // C99 may pass any representation for its enum. Reading an invalid
        // C++ enum lvalue before validation is UB; preserve the frozen field's
        // layout while checking its raw underlying tag without that read.
        std::underlying_type_t<iprop_uniform_type> raw{};
        std::memcpy(&raw, &uniform.type, sizeof(raw));
        // Match the old enum's integer promotion and >U32 comparison, including
        // its negative-tag default-color fallback. Do not widen legacy policy
        // into the exact-membership policy reserved for new parameters.
        const auto type = static_cast<decltype(+IPROP_UNIFORM_COLOR)>(raw);
        if (uniform.name == nullptr || type > IPROP_UNIFORM_U32)
            return CallOutcome::argument("uniform name or type is invalid");
        // Frozen behavior: first duplicate wins and unknown names are ignored
        // by the same VM input model. Strict parameters belong to P.5.
        inputs.uniforms.emplace(uniform.name, toUniform(uniform, type));
    }
    return {};
}

void generateProgram(const iprop_program& program, const GenerationInputs& inputs,
                     iprop_result& result, ErrorContext& context) {
    context.file = &program.file;
    context.stage = ErrorStage::Arguments;
    VM vm(program.file, inputs);
    context.stage = ErrorStage::Execute;
    vm.run();
    packResult(result, vm.takeResult(), program.file);
}

iprop_program_info_ex programInfo(const iprop_program& program) noexcept {
    const auto& meta = program.file.meta;
    iprop_program_info_ex out{};
    out.size_x = meta.sizeX;
    out.size_y = meta.sizeY;
    out.origin_x = meta.origin.x;
    out.origin_y = meta.origin.y;
    out.variants = meta.variants;
    out.archetypes = meta.archetypes;
    out.fuel = meta.fuel;
    out.has_emission = meta.hasEmission ? 1 : 0;
    if (meta.hasEmission) {
        out.emission_color = {meta.emission.color.r, meta.emission.color.g,
                              meta.emission.color.b, meta.emission.color.a};
        // Reporting metadata performs no arithmetic or canonicalization.
        std::memcpy(&out.emission_offset_x, &meta.emission.offset.x, sizeof(float));
        std::memcpy(&out.emission_offset_y, &meta.emission.offset.y, sizeof(float));
        std::memcpy(&out.emission_radius, &meta.emission.radius, sizeof(float));
    }
    const auto& header = program.file.containerHeader;
    out.format_major = header.versionMajor;
    out.format_minor = header.versionMinor;
    out.container_flags = header.flags;
    return out;
}

iprop_meta legacyMeta(const iprop_program& program) noexcept {
    const auto info = programInfo(program);
    return {info.size_x, info.size_y, info.origin_x, info.origin_y,
            info.variants, info.archetypes, info.fuel};
}

iprop_image resultImage(const iprop_result& result) noexcept {
    const auto& canvas = result.image;
    iprop_image out{};
    out.width = canvas.width;
    out.height = canvas.height;
    out.pixels = canvas.pixels.empty() ? nullptr : reinterpret_cast<const uint8_t*>(canvas.pixels.data());
    out.output_count = static_cast<uint32_t>(result.outputs.size());
    out.outputs = result.outputs.empty() ? nullptr : result.outputs.data();
    return out;
}

} // namespace iprop::capi
