// The C API of models (preview, API 0.3): adapters over iprop::model that
// share the checked boundary, the sized descriptors and the parameter sets
// of the 2D C API. Nothing here changes `ipropc`.
#include "iprop3d.h"

#include "CheckedBoundary.hpp"
#include "ExecutionOptions.hpp"
#include "Handles.hpp"
#include "Utf8View.hpp"
#include "iprop/model/Model.hpp"

#include <array>
#include <cstddef>
#include <limits>
#include <memory>

static_assert(IPROP3D_ALPHA_OPAQUE == static_cast<uint32_t>(iprop::model::SkinAlpha::Opaque));
static_assert(IPROP3D_ALPHA_CUTOUT == static_cast<uint32_t>(iprop::model::SkinAlpha::Cutout));
static_assert(IPROP3D_ALPHA_BLEND == static_cast<uint32_t>(iprop::model::SkinAlpha::Blend));
static_assert(sizeof(iprop::Color) == 4);
static_assert(sizeof(iprop::f32) == sizeof(float) && sizeof(iprop::u32) == sizeof(uint32_t));

struct iprop3d_model {
    std::shared_ptr<const iprop::model::LoadedModel> data;
    // The model program as the 2D API sees a program: its inputs and the
    // identity of its parameter sets. It shares ownership of `data`.
    iprop_program root;
};

struct iprop3d_result {
    iprop::model::ModelResult data;
};

namespace {
using namespace iprop::capi;

// A failed argument check outside any other boundary, reported as one.
iprop_status_ex reject(iprop_error_ex* error, const void* output, std::size_t outputSize,
                       std::string_view message) noexcept {
    ErrorContext context{iprop::ErrorStage::Arguments};
    return checkedBoundary(error, output, outputSize, context, [&] { return CallOutcome::argument(message); });
}

template<class Descriptor>
void writeName(Descriptor* out, uint32_t capacity, const std::string& name) noexcept {
    const char* data = name.empty() ? nullptr : name.data();
    writeField(out, capacity, offsetof(Descriptor, name) + offsetof(iprop_utf8_view, data), data);
    writeField(out, capacity, offsetof(Descriptor, name) + offsetof(iprop_utf8_view, byte_count),
               static_cast<uint32_t>(name.size()));
}

template<class Descriptor>
void clearName(Descriptor* out, uint32_t capacity) noexcept {
    const iprop_utf8_view none{};
    writeField(out, capacity, offsetof(Descriptor, name) + offsetof(iprop_utf8_view, data), none.data);
    writeField(out, capacity, offsetof(Descriptor, name) + offsetof(iprop_utf8_view, byte_count), none.byte_count);
}

// A query output: its capacity when it is usable and its flags are zero.
template<class Descriptor>
CallOutcome queryOutput(const Descriptor* out, uint32_t& capacity, std::string_view what) noexcept {
    if (!descriptorSize(out, capacity)) return CallOutcome::argument(what);
    if (readField<uint32_t>(out, capacity, offsetof(Descriptor, flags)) != 0)
        return CallOutcome::argument("descriptor flags are unsupported");
    return {};
}

template<class T> const T* dataOrNull(const std::vector<T>& values) noexcept {
    return values.empty() ? nullptr : values.data();
}
} // namespace

extern "C" {

// ---- initializers -----------------------------------------------------------

#define IPROP3D_INIT_BEGIN(type)                                                     \
    iprop_status_ex type##_init(type* out, uint32_t capacity) {                      \
        if (out == nullptr || capacity < sizeof(uint32_t)) return IPROP_EX_E_ARGUMENT; \
        writeField(out, capacity, 0, capacity);                                      \
        const type defaults{};                                                       \
        (void)defaults;
#define ZERO(type, field) writeField(out, capacity, offsetof(type, field), defaults.field)
#define IPROP3D_INIT_END \
        return IPROP_EX_OK; \
    }

IPROP3D_INIT_BEGIN(iprop3d_library_info)
    ZERO(iprop3d_library_info, api_major); ZERO(iprop3d_library_info, api_minor);
    ZERO(iprop3d_library_info, max_vertices); ZERO(iprop3d_library_info, max_triangles);
    ZERO(iprop3d_library_info, max_textures); ZERO(iprop3d_library_info, max_texture_side);
    ZERO(iprop3d_library_info, max_texels);
IPROP3D_INIT_END

IPROP3D_INIT_BEGIN(iprop3d_model_info)
    ZERO(iprop3d_model_info, flags); ZERO(iprop3d_model_info, size_x); ZERO(iprop3d_model_info, size_y);
    ZERO(iprop3d_model_info, size_z); ZERO(iprop3d_model_info, units_per_meter); ZERO(iprop3d_model_info, fuel);
    ZERO(iprop3d_model_info, input_count); ZERO(iprop3d_model_info, skin_program_count);
    ZERO(iprop3d_model_info, container_flags);
IPROP3D_INIT_END

IPROP3D_INIT_BEGIN(iprop3d_skin_program_info)
    ZERO(iprop3d_skin_program_info, flags); ZERO(iprop3d_skin_program_info, program_index);
    ZERO(iprop3d_skin_program_info, width); ZERO(iprop3d_skin_program_info, height);
    clearName(out, capacity);
IPROP3D_INIT_END

IPROP3D_INIT_BEGIN(iprop3d_generate_options)
    ZERO(iprop3d_generate_options, flags); ZERO(iprop3d_generate_options, seed);
    ZERO(iprop3d_generate_options, lod); ZERO(iprop3d_generate_options, known_textures);
    ZERO(iprop3d_generate_options, known_texture_count);
IPROP3D_INIT_END

IPROP3D_INIT_BEGIN(iprop3d_result_info)
    ZERO(iprop3d_result_info, flags); ZERO(iprop3d_result_info, bounds_min); ZERO(iprop3d_result_info, bounds_max);
    ZERO(iprop3d_result_info, size); ZERO(iprop3d_result_info, units_per_meter);
    ZERO(iprop3d_result_info, vertex_count); ZERO(iprop3d_result_info, triangle_count);
    ZERO(iprop3d_result_info, surface_count); ZERO(iprop3d_result_info, texture_count);
    ZERO(iprop3d_result_info, fuel_budget); ZERO(iprop3d_result_info, fuel_used);
    ZERO(iprop3d_result_info, work_budget); ZERO(iprop3d_result_info, work_used);
    ZERO(iprop3d_result_info, output_count);
IPROP3D_INIT_END

IPROP3D_INIT_BEGIN(iprop3d_output_view)
    ZERO(iprop3d_output_view, flags); ZERO(iprop3d_output_view, output_index); ZERO(iprop3d_output_view, kind);
    ZERO(iprop3d_output_view, list); ZERO(iprop3d_output_view, value_count);
    ZERO(iprop3d_output_view, floats_per_value);
    clearName(out, capacity);
    ZERO(iprop3d_output_view, values);
IPROP3D_INIT_END

IPROP3D_INIT_BEGIN(iprop3d_surface_view)
    ZERO(iprop3d_surface_view, flags); ZERO(iprop3d_surface_view, surface_index);
    ZERO(iprop3d_surface_view, texture_index); ZERO(iprop3d_surface_view, roughness);
    ZERO(iprop3d_surface_view, metallic); ZERO(iprop3d_surface_view, emission);
    ZERO(iprop3d_surface_view, alpha_mode); ZERO(iprop3d_surface_view, alpha_cutoff);
    ZERO(iprop3d_surface_view, two_sided); ZERO(iprop3d_surface_view, crisp);
    ZERO(iprop3d_surface_view, vertex_count); ZERO(iprop3d_surface_view, index_count);
    ZERO(iprop3d_surface_view, positions); ZERO(iprop3d_surface_view, normals);
    ZERO(iprop3d_surface_view, tangents); ZERO(iprop3d_surface_view, uvs); ZERO(iprop3d_surface_view, indices);
IPROP3D_INIT_END

IPROP3D_INIT_BEGIN(iprop3d_texture_view)
    ZERO(iprop3d_texture_view, flags); ZERO(iprop3d_texture_view, texture_index);
    ZERO(iprop3d_texture_view, repeats); ZERO(iprop3d_texture_view, key);
    clearName(out, capacity);
    ZERO(iprop3d_texture_view, asset_width); ZERO(iprop3d_texture_view, asset_height);
    ZERO(iprop3d_texture_view, width); ZERO(iprop3d_texture_view, height);
    ZERO(iprop3d_texture_view, byte_count); ZERO(iprop3d_texture_view, row_stride);
    ZERO(iprop3d_texture_view, pixel_format); ZERO(iprop3d_texture_view, alpha_mode);
    ZERO(iprop3d_texture_view, origin); ZERO(iprop3d_texture_view, image_count);
    ZERO(iprop3d_texture_view, pixels);
    ZERO(iprop3d_texture_view, fuel); ZERO(iprop3d_texture_view, work);
    ZERO(iprop3d_texture_view, image_kinds); ZERO(iprop3d_texture_view, reused);
IPROP3D_INIT_END

#undef IPROP3D_INIT_BEGIN
#undef IPROP3D_INIT_END
#undef ZERO

// ---- the library ------------------------------------------------------------

iprop_status_ex iprop3d_library_query(iprop3d_library_info* out, iprop_error_ex* error) {
    ErrorContext context{iprop::ErrorStage::Arguments};
    return checkedBoundary(error, out, queryCapacity(out), context, [&] {
        uint32_t capacity = 0;
        if (!descriptorSize(out, capacity))
            return CallOutcome::argument("library info output is null or smaller than four bytes");
        iprop3d_library_info info{};
        info.api_major = IPROP3D_API_VERSION_MAJOR;
        info.api_minor = IPROP3D_API_VERSION_MINOR;
        info.max_vertices = iprop::model::kMaxVertices;
        info.max_triangles = iprop::model::kMaxTriangles;
        info.max_textures = iprop::model::kMaxTextures;
        info.max_texture_side = iprop::model::kMaxTextureSide;
        info.max_texels = iprop::model::kMaxTexels;
#define WRITE(field) writeField(out, capacity, offsetof(iprop3d_library_info, field), info.field)
        WRITE(api_major); WRITE(api_minor); WRITE(max_vertices); WRITE(max_triangles);
        WRITE(max_textures); WRITE(max_texture_side); WRITE(max_texels);
#undef WRITE
        return CallOutcome{};
    });
}

// ---- models -----------------------------------------------------------------

iprop_status_ex iprop3d_model_load(const uint8_t* bytes, uint64_t byte_count, iprop3d_model** out_model,
                                   iprop_error_ex* error) {
    // This owner survives the guarded callable and every diagnostic catch,
    // and with it the file a located error refers to.
    std::unique_ptr<iprop3d_model> model;
    ErrorContext context{iprop::ErrorStage::Load};
    return checkedBoundary(error, out_model, sizeof(iprop3d_model*), context, [&] {
        if (bytes == nullptr) return CallOutcome::argument("bytes is null");
        if (out_model == nullptr) return CallOutcome::argument("out_model is null");
        if (byte_count > std::numeric_limits<std::size_t>::max())
            return CallOutcome::argument("byte_count cannot be represented on this platform");
        model = std::make_unique<iprop3d_model>();
        auto data = std::make_shared<iprop::model::LoadedModel>();
        model->data = data;
        *data = iprop::model::loadModel({bytes, static_cast<std::size_t>(byte_count)});
        context.file = &data->file;
        context.stage = iprop::ErrorStage::Validate;
        iprop::model::validateModel(*data);
        model->root.data = std::shared_ptr<const iprop::LoadedFile>(model->data, &model->data->file);
        *out_model = model.release();
        return CallOutcome{};
    });
}

void iprop3d_model_free(iprop3d_model* model) { delete model; }

iprop_status_ex iprop3d_model_query(const iprop3d_model* model, iprop3d_model_info* out, iprop_error_ex* error) {
    ErrorContext context{iprop::ErrorStage::Arguments};
    return checkedBoundary(error, out, queryCapacity(out), context, [&] {
        if (model == nullptr) return CallOutcome::argument("model is null");
        uint32_t capacity = 0;
        if (const auto usable = queryOutput(out, capacity, "model info output is null or smaller than four bytes");
            !usable.succeeded)
            return usable;
        const auto& data = *model->data;
        iprop3d_model_info info{};
        info.size_x = data.size[0];
        info.size_y = data.size[1];
        info.size_z = data.size[2];
        info.units_per_meter = data.unitsPerMeter;
        info.fuel = data.file.meta.fuel;
        info.input_count = static_cast<uint32_t>(data.file.meta.inputs.size());
        info.skin_program_count = static_cast<uint32_t>(data.skins.size());
        info.container_flags = data.file.containerHeader.flags;
#define WRITE(field) writeField(out, capacity, offsetof(iprop3d_model_info, field), info.field)
        WRITE(size_x); WRITE(size_y); WRITE(size_z); WRITE(units_per_meter); WRITE(fuel);
        WRITE(input_count); WRITE(skin_program_count); WRITE(container_flags);
#undef WRITE
        return CallOutcome{};
    });
}

iprop_status_ex iprop3d_model_skin_program(const iprop3d_model* model, uint32_t program_index,
                                           iprop3d_skin_program_info* out, iprop_error_ex* error) {
    ErrorContext context{iprop::ErrorStage::Arguments};
    return checkedBoundary(error, out, queryCapacity(out), context, [&] {
        if (model == nullptr) return CallOutcome::argument("model is null");
        uint32_t capacity = 0;
        if (const auto usable = queryOutput(out, capacity, "skin program output is null or smaller than four bytes");
            !usable.succeeded)
            return usable;
        if (program_index >= model->data->skins.size()) return CallOutcome::argument("skin program index is out of range");
        const auto& program = model->data->skins[program_index];
        writeField(out, capacity, offsetof(iprop3d_skin_program_info, program_index), program_index);
        writeField(out, capacity, offsetof(iprop3d_skin_program_info, width), uint32_t{program.file.meta.sizeX});
        writeField(out, capacity, offsetof(iprop3d_skin_program_info, height), uint32_t{program.file.meta.sizeY});
        writeName(out, capacity, program.name);
        return CallOutcome{};
    });
}

iprop_status_ex iprop3d_model_input_count(const iprop3d_model* model, uint32_t* out_count, iprop_error_ex* error) {
    if (model == nullptr) return reject(error, out_count, sizeof(uint32_t), "model is null");
    return iprop_program_input_count(&model->root, out_count, error);
}

iprop_status_ex iprop3d_model_input_get(const iprop3d_model* model, uint32_t input_index,
                                        iprop_input_info_ex* out, iprop_error_ex* error) {
    if (model == nullptr) return reject(error, out, queryCapacity(out), "model is null");
    return iprop_program_input_get(&model->root, input_index, out, error);
}

iprop_status_ex iprop3d_model_input_find(const iprop3d_model* model, iprop_utf8_view name, uint32_t* out_index,
                                         iprop_error_ex* error) {
    if (model == nullptr) return reject(error, out_index, sizeof(uint32_t), "model is null");
    return iprop_program_input_find(&model->root, name, out_index, error);
}

iprop_status_ex iprop3d_model_parameters_create(const iprop3d_model* model, iprop_parameters** out_parameters,
                                                iprop_error_ex* error) {
    if (model == nullptr) return reject(error, out_parameters, sizeof(iprop_parameters*), "model is null");
    return iprop_parameters_create(&model->root, out_parameters, error);
}

// ---- generation -------------------------------------------------------------

iprop_status_ex iprop3d_generate(const iprop3d_model* model, const iprop3d_generate_options* options,
                                 const iprop_parameters* parameters, const iprop_execution_limits_ex* limits,
                                 iprop3d_result** out_result, iprop_error_ex* error) {
    // These owners survive the guarded callable and every diagnostic catch.
    std::unique_ptr<iprop3d_result> result;
    ErrorContext context{iprop::ErrorStage::Arguments, model != nullptr ? &model->data->file : nullptr};
    // The known textures (API 0.3), when the options are large enough to
    // name them; the array is input storage like the options themselves.
    const iprop3d_known_texture* known = nullptr;
    uint32_t knownCount = 0;
    if (uint32_t size = 0; options != nullptr && descriptorSize(options, size)) {
        known = readField<const iprop3d_known_texture*>(options, size, offsetof(iprop3d_generate_options, known_textures));
        knownCount = readField<uint32_t>(options, size, offsetof(iprop3d_generate_options, known_texture_count));
    }
    const std::array spans{ProtectedSpan{model, model != nullptr ? sizeof(*model) : 0},
        ProtectedSpan{options, queryCapacity(options)},
        ProtectedSpan{parameters, parameters != nullptr ? sizeof(*parameters) : 0},
        ProtectedSpan{parameters != nullptr ? parameters->values.data() : nullptr,
                      parameters != nullptr ? parameters->values.size() * sizeof(iprop::UniformValue) : 0},
        ProtectedSpan{limits, queryCapacity(limits)},
        ProtectedSpan{known, known != nullptr ? std::size_t{knownCount} * sizeof(iprop3d_known_texture) : 0}};
    return checkedBoundary(error, out_result, sizeof(iprop3d_result*), context, [&] {
        if (model == nullptr) return CallOutcome::argument("model is null");
        if (out_result == nullptr) return CallOutcome::argument("out_result is null");
        uint32_t size = 0;
        if (options == nullptr) return CallOutcome::argument("generation options are null");
        if (!descriptorSize(options, size)) return CallOutcome::argument("generation options are smaller than four bytes");
        if (readField<uint32_t>(options, size, offsetof(iprop3d_generate_options, flags)) != 0)
            return CallOutcome::argument("generation option flags are unsupported");
        if (parameters != nullptr && !parametersCompatible(model->root, *parameters))
            return CallOutcome::argument("parameters belong to another model load", iprop::ErrorReason::ForeignParameters);
        // The request stays on this stack until the generation has finished:
        // the limits keep a pointer to it for the host's poll.
        ExecutionRequest execution;
        if (const auto control = readExecutionRequest(limits, execution); !control.succeeded) return control;
        iprop::model::ModelInputs inputs;
        inputs.seed = readField<uint32_t>(options, size, offsetof(iprop3d_generate_options, seed));
        inputs.lod = readField<uint32_t>(options, size, offsetof(iprop3d_generate_options, lod));
        if (inputs.lod > IPROP3D_MAX_LOD) return CallOutcome::argument("lod is outside 0..7");
        if (knownCount != 0 && known == nullptr) return CallOutcome::argument("known textures are null");
        std::vector<iprop::model::KnownTexture> held;
        held.reserve(knownCount);
        for (uint32_t i = 0; i < knownCount; ++i) {
            if (known[i].reserved != 0 || (known[i].image_kinds & ~uint32_t{0x3E}) != 0)
                return CallOutcome::argument("a known texture has unsupported fields");
            held.push_back({known[i].key, known[i].fuel, known[i].work, known[i].image_kinds});
        }
        inputs.knownTextures = held;
        inputs.limits = execution.limits;
        result = std::make_unique<iprop3d_result>();
        context.stage = iprop::ErrorStage::Execute;
        // Parameters are read only, by input ID; NULL selects the defaults.
        result->data = parameters != nullptr
            ? iprop::model::generateModel(*model->data, inputs, std::span<const iprop::UniformValue>(parameters->values))
            : iprop::model::generateModel(*model->data, inputs);
        *out_result = result.release();
        return CallOutcome{};
    }, spans);
}

void iprop3d_result_free(iprop3d_result* result) { delete result; }

uint64_t iprop3d_texture_stamp(void) { return iprop::model::textureStamp(); }

// ---- results ----------------------------------------------------------------

iprop_status_ex iprop3d_result_query(const iprop3d_result* result, iprop3d_result_info* out, iprop_error_ex* error) {
    ErrorContext context{iprop::ErrorStage::Arguments};
    return checkedBoundary(error, out, queryCapacity(out), context, [&] {
        if (result == nullptr) return CallOutcome::argument("result is null");
        uint32_t capacity = 0;
        if (const auto usable = queryOutput(out, capacity, "result info output is null or smaller than four bytes");
            !usable.succeeded)
            return usable;
        const auto& data = result->data;
        iprop3d_result_info info{};
        for (int axis = 0; axis < 3; ++axis) {
            info.bounds_min[axis] = data.boundsMin[axis];
            info.bounds_max[axis] = data.boundsMax[axis];
            info.size[axis] = data.size[axis];
        }
        info.units_per_meter = data.unitsPerMeter;
        info.vertex_count = data.vertexCount;
        info.triangle_count = data.triangleCount;
        info.surface_count = static_cast<uint32_t>(data.surfaces.size());
        info.texture_count = static_cast<uint32_t>(data.textures.size());
        info.fuel_budget = data.fuelBudget;
        info.fuel_used = data.fuelUsed;
        info.work_budget = data.workBudget;
        info.work_used = data.workUsed;
        info.output_count = static_cast<uint32_t>(data.outputs.size());
#define WRITE(field) writeField(out, capacity, offsetof(iprop3d_result_info, field), info.field)
        WRITE(bounds_min); WRITE(bounds_max); WRITE(size); WRITE(units_per_meter);
        WRITE(vertex_count); WRITE(triangle_count); WRITE(surface_count); WRITE(texture_count);
        WRITE(fuel_budget); WRITE(fuel_used); WRITE(work_budget); WRITE(work_used); WRITE(output_count);
#undef WRITE
        return CallOutcome{};
    });
}

iprop_status_ex iprop3d_result_surface(const iprop3d_result* result, uint32_t surface_index,
                                       iprop3d_surface_view* out, iprop_error_ex* error) {
    ErrorContext context{iprop::ErrorStage::Arguments};
    return checkedBoundary(error, out, queryCapacity(out), context, [&] {
        if (result == nullptr) return CallOutcome::argument("result is null");
        uint32_t capacity = 0;
        if (const auto usable = queryOutput(out, capacity, "surface view is null or smaller than four bytes");
            !usable.succeeded)
            return usable;
        if (surface_index >= result->data.surfaces.size()) return CallOutcome::argument("surface index is out of range");
        const auto& surface = result->data.surfaces[surface_index];
        iprop3d_surface_view view{};
        view.surface_index = surface_index;
        view.texture_index = surface.texture;
        view.roughness = surface.roughness;
        view.metallic = surface.metallic;
        view.emission = surface.emission;
        view.alpha_mode = static_cast<iprop3d_alpha_mode>(surface.alpha);
        view.alpha_cutoff = surface.cutoff;
        view.two_sided = surface.twoSided ? 1 : 0;
        view.crisp = surface.crisp ? 1 : 0;
        view.vertex_count = static_cast<uint32_t>(surface.positions.size() / 3);
        view.index_count = static_cast<uint32_t>(surface.indices.size());
        view.positions = dataOrNull(surface.positions);
        view.normals = dataOrNull(surface.normals);
        view.tangents = dataOrNull(surface.tangents);
        view.uvs = dataOrNull(surface.uvs);
        view.indices = dataOrNull(surface.indices);
#define WRITE(field) writeField(out, capacity, offsetof(iprop3d_surface_view, field), view.field)
        WRITE(surface_index); WRITE(texture_index); WRITE(roughness); WRITE(metallic); WRITE(emission);
        WRITE(alpha_mode); WRITE(alpha_cutoff); WRITE(two_sided); WRITE(crisp);
        WRITE(vertex_count); WRITE(index_count);
        WRITE(positions); WRITE(normals); WRITE(tangents); WRITE(uvs); WRITE(indices);
#undef WRITE
        return CallOutcome{};
    });
}

iprop_status_ex iprop3d_result_texture(const iprop3d_result* result, uint32_t texture_index,
                                       iprop3d_texture_view* out, iprop_error_ex* error) {
    ErrorContext context{iprop::ErrorStage::Arguments};
    return checkedBoundary(error, out, queryCapacity(out), context, [&] {
        if (result == nullptr) return CallOutcome::argument("result is null");
        uint32_t capacity = 0;
        if (const auto usable = queryOutput(out, capacity, "texture view is null or smaller than four bytes");
            !usable.succeeded)
            return usable;
        if (texture_index >= result->data.textures.size()) return CallOutcome::argument("texture index is out of range");
        const auto& texture = result->data.textures[texture_index];
        iprop3d_texture_view view{};
        view.texture_index = texture_index;
        view.repeats = texture.repeats ? 1 : 0;
        view.key = texture.key;
        view.asset_width = texture.assetWidth;
        view.asset_height = texture.assetHeight;
        view.width = texture.color.width;
        view.height = texture.color.height;
        view.byte_count = uint64_t{texture.color.pixels.size()} * sizeof(iprop::Color);
        view.row_stride = uint64_t{texture.color.width} * sizeof(iprop::Color);
        view.pixel_format = IPROP_PIXEL_FORMAT_RGBA8;
        // The main image of a 2D generation, with its compositor's alpha.
        view.alpha_mode = IPROP_IMAGE_ALPHA_LEGACY_DESTINATION;
        view.origin = IPROP_IMAGE_ORIGIN_TOP_LEFT;
        view.image_count = static_cast<uint32_t>(texture.images.size());
        view.pixels = texture.color.pixels.empty() ? nullptr
            : reinterpret_cast<const uint8_t*>(texture.color.pixels.data());
        view.fuel = texture.fuel;
        view.work = texture.work;
        view.image_kinds = texture.kinds;
        view.reused = texture.reused ? 1 : 0;
#define WRITE(field) writeField(out, capacity, offsetof(iprop3d_texture_view, field), view.field)
        WRITE(texture_index); WRITE(repeats); WRITE(key); WRITE(asset_width); WRITE(asset_height);
        WRITE(width); WRITE(height); WRITE(byte_count); WRITE(row_stride);
        WRITE(pixel_format); WRITE(alpha_mode); WRITE(origin); WRITE(image_count); WRITE(pixels);
        WRITE(fuel); WRITE(work); WRITE(image_kinds); WRITE(reused);
#undef WRITE
        writeName(out, capacity, texture.name);
        return CallOutcome{};
    });
}

iprop_status_ex iprop3d_result_texture_image(const iprop3d_result* result, uint32_t texture_index,
                                             uint32_t image_index, iprop_aux_image_view_ex* out,
                                             iprop_error_ex* error) {
    ErrorContext context{iprop::ErrorStage::Arguments};
    return checkedBoundary(error, out, queryCapacity(out), context, [&] {
        if (result == nullptr) return CallOutcome::argument("result is null");
        uint32_t capacity = 0;
        if (!descriptorSize(out, capacity))
            return CallOutcome::argument("auxiliary image view is null or smaller than four bytes");
        if (readField<uint32_t>(out, capacity, offsetof(iprop_aux_image_view_ex, flags)) != 0)
            return CallOutcome::argument("auxiliary image view flags are unsupported");
        if (texture_index >= result->data.textures.size()) return CallOutcome::argument("texture index is out of range");
        const auto& images = result->data.textures[texture_index].images;
        if (image_index >= images.size()) return CallOutcome::argument("auxiliary image index is out of range");
        const auto& image = images[image_index];
        iprop_aux_image_view_ex view{};
        view.image_index = image_index;
        view.kind = static_cast<iprop_aux_image_kind>(image.kind);
        view.source = static_cast<iprop_aux_image_source>(image.source);
        view.width = image.image.width;
        view.height = image.image.height;
        view.byte_count = uint64_t{image.image.pixels.size()} * sizeof(iprop::Color);
        view.row_stride = uint64_t{image.image.width} * sizeof(iprop::Color);
        view.pixel_format = IPROP_PIXEL_FORMAT_RGBA8;
        view.alpha_mode = IPROP_IMAGE_ALPHA_OPAQUE;
        view.origin = IPROP_IMAGE_ORIGIN_TOP_LEFT;
        view.pixels = image.image.pixels.empty() ? nullptr
            : reinterpret_cast<const uint8_t*>(image.image.pixels.data());
#define WRITE(field) writeField(out, capacity, offsetof(iprop_aux_image_view_ex, field), view.field)
        WRITE(image_index); WRITE(kind); WRITE(source); WRITE(width); WRITE(height);
        WRITE(byte_count); WRITE(row_stride); WRITE(pixel_format); WRITE(alpha_mode); WRITE(origin); WRITE(pixels);
#undef WRITE
        writeName(out, capacity, image.name);
        return CallOutcome{};
    });
}

iprop_status_ex iprop3d_result_texture_image_find(const iprop3d_result* result, uint32_t texture_index,
                                                  iprop_aux_image_kind kind, uint32_t* out_index,
                                                  iprop_error_ex* error) {
    ErrorContext context{iprop::ErrorStage::Arguments};
    return checkedBoundary(error, out_index, sizeof(uint32_t), context, [&] {
        if (result == nullptr) return CallOutcome::argument("result is null");
        if (out_index == nullptr) return CallOutcome::argument("out_index is null");
        if (texture_index >= result->data.textures.size()) return CallOutcome::argument("texture index is out of range");
        const auto& images = result->data.textures[texture_index].images;
        for (std::size_t index = 0; index < images.size(); ++index) {
            if (static_cast<iprop_aux_image_kind>(images[index].kind) == kind) {
                *out_index = static_cast<uint32_t>(index);
                return CallOutcome{};
            }
        }
        return CallOutcome::argument("the texture has no auxiliary image of this kind");
    });
}

// ---- outputs (API 0.2) --------------------------------------------------------

static_assert(sizeof(iprop::Vec3) == 3 * sizeof(float), "a point is three floats");
static_assert(sizeof(iprop::model::ModelBox) == 15 * sizeof(float), "a box is fifteen floats");

iprop_status_ex iprop3d_result_output(const iprop3d_result* result, uint32_t output_index,
                                      iprop3d_output_view* out, iprop_error_ex* error) {
    ErrorContext context{iprop::ErrorStage::Arguments};
    return checkedBoundary(error, out, queryCapacity(out), context, [&] {
        if (result == nullptr) return CallOutcome::argument("result is null");
        uint32_t capacity = 0;
        if (const auto usable = queryOutput(out, capacity, "output view is null or smaller than four bytes");
            !usable.succeeded)
            return usable;
        if (output_index >= result->data.outputs.size()) return CallOutcome::argument("output index is out of range");
        const auto& output = result->data.outputs[output_index];
        const bool box = output.kind == iprop::model::ModelOutputKind::Box3d;
        iprop3d_output_view view{};
        view.output_index = output_index;
        view.kind = box ? IPROP3D_OUTPUT_BOX3D : IPROP3D_OUTPUT_POINT3D;
        view.list = output.list ? 1 : 0;
        view.value_count = static_cast<uint32_t>(box ? output.boxes.size() : output.points.size());
        view.floats_per_value = box ? 15 : 3;
        if (view.value_count != 0)
            view.values = box ? reinterpret_cast<const float*>(output.boxes.data())
                              : reinterpret_cast<const float*>(output.points.data());
#define WRITE(field) writeField(out, capacity, offsetof(iprop3d_output_view, field), view.field)
        WRITE(output_index); WRITE(kind); WRITE(list); WRITE(value_count); WRITE(floats_per_value); WRITE(values);
#undef WRITE
        writeName(out, capacity, output.name);
        return CallOutcome{};
    });
}

iprop_status_ex iprop3d_result_output_find(const iprop3d_result* result, iprop_utf8_view name,
                                           uint32_t* out_index, iprop_error_ex* error) {
    ErrorContext context{iprop::ErrorStage::Arguments};
    return checkedBoundary(error, out_index, sizeof(uint32_t), context, [&] {
        if (result == nullptr) return CallOutcome::argument("result is null");
        if (out_index == nullptr) return CallOutcome::argument("out_index is null");
        std::string_view sought;
        if (!readUtf8View(name, sought)) return CallOutcome::argument("output name is not a valid UTF-8 view");
        const auto& outputs = result->data.outputs;
        for (std::size_t index = 0; index < outputs.size(); ++index) {
            if (outputs[index].name == sought) {
                *out_index = static_cast<uint32_t>(index);
                return CallOutcome{};
            }
        }
        return CallOutcome::argument("output name was not found");
    });
}

} // extern "C"
