// Models in the GDExtension (see iprop_model_godot.hpp).

#include "iprop_model_godot.hpp"
#include "godot_support.hpp"

#include <godot_cpp/classes/base_material3d.hpp>
#include <godot_cpp/classes/box_shape3d.hpp>
#include <godot_cpp/classes/collision_shape3d.hpp>
#include <godot_cpp/classes/marker3d.hpp>
#include <godot_cpp/classes/static_body3d.hpp>
#include <godot_cpp/variant/basis.hpp>
#include <godot_cpp/variant/transform3d.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/image_texture.hpp>
#include <godot_cpp/classes/mesh.hpp>
#include <godot_cpp/classes/standard_material3d.hpp>
#include <godot_cpp/variant/packed_float32_array.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>
#include <godot_cpp/variant/packed_vector2_array.hpp>
#include <godot_cpp/variant/packed_vector3_array.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <memory>
#include <vector>

using namespace godot;

namespace iprop_godot {

namespace {

// An owned Godot image of borrowed RGBA8 pixels, or null.
Ref<Image> imageOf(uint32_t width, uint32_t height, uint64_t byte_count, const uint8_t* pixels) {
    if (width == 0 || height == 0 || byte_count != uint64_t{width} * height * 4 ||
        width > static_cast<uint32_t>(std::numeric_limits<int>::max()) ||
        height > static_cast<uint32_t>(std::numeric_limits<int>::max()))
        return {};
    PackedByteArray bytes;
    if (bytes.resize(static_cast<int64_t>(byte_count)) != OK) return {};
    std::memcpy(bytes.ptrw(), pixels, static_cast<size_t>(byte_count));
    return Image::create_from_data(static_cast<int>(width), static_cast<int>(height), false, Image::FORMAT_RGBA8, bytes);
}

// A GPU texture of an image, with the mipmaps smooth filtering reads. The
// image handed out as a property stays as generated.
Ref<ImageTexture> textureOf(const Ref<Image>& image, bool normal) {
    const Ref<Image> copy = image->duplicate();
    // Normal maps keep unit length down the mipmap chain.
    copy->generate_mipmaps(normal);
    return ImageTexture::create_from_image(copy);
}

template<class Packed, class T> bool copyInto(Packed& out, const T* data, size_t count) {
    if (out.resize(static_cast<int64_t>(count)) != OK) return false;
    if (count != 0) std::memcpy(out.ptrw(), data, count * sizeof(T));
    return true;
}

} // namespace

// ---- IpropModel -----------------------------------------------------------

void IpropModel::_bind_methods() {
    ClassDB::bind_method(D_METHOD("load_file", "path"), &IpropModel::load_file);
    ClassDB::bind_method(D_METHOD("is_loaded"), &IpropModel::is_loaded);
    ClassDB::bind_method(D_METHOD("get_inputs"), &IpropModel::get_inputs);
    ClassDB::bind_method(D_METHOD("set_input", "name", "value"), &IpropModel::set_input);
    ClassDB::bind_method(D_METHOD("reset_inputs"), &IpropModel::reset_inputs);
    ClassDB::bind_method(D_METHOD("generate", "seed", "limits"), &IpropModel::generate, DEFVAL(Dictionary()));
    ClassDB::bind_method(D_METHOD("get_size"), &IpropModel::get_size);
    ClassDB::bind_method(D_METHOD("get_units_per_meter"), &IpropModel::get_units_per_meter);
    ClassDB::bind_method(D_METHOD("get_fuel"), &IpropModel::get_fuel);
    ClassDB::bind_method(D_METHOD("get_skin_programs"), &IpropModel::get_skin_programs);
    ClassDB::bind_method(D_METHOD("get_surfaces"), &IpropModel::get_surfaces);
    ClassDB::bind_method(D_METHOD("get_textures"), &IpropModel::get_textures);
    ClassDB::bind_method(D_METHOD("get_outputs"), &IpropModel::get_outputs);
    ClassDB::bind_method(D_METHOD("get_bounds_min"), &IpropModel::get_bounds_min);
    ClassDB::bind_method(D_METHOD("get_bounds_max"), &IpropModel::get_bounds_max);
    ClassDB::bind_method(D_METHOD("get_vertex_count"), &IpropModel::get_vertex_count);
    ClassDB::bind_method(D_METHOD("get_triangle_count"), &IpropModel::get_triangle_count);
    ClassDB::bind_method(D_METHOD("get_fuel_used"), &IpropModel::get_fuel_used);
    ClassDB::bind_method(D_METHOD("get_work"), &IpropModel::get_work);
    ClassDB::bind_method(D_METHOD("get_last_error"), &IpropModel::get_last_error);
    ClassDB::bind_method(D_METHOD("clear_texture_cache"), &IpropModel::clear_texture_cache);
    ClassDB::bind_method(D_METHOD("get_cached_texture_count"), &IpropModel::get_cached_texture_count);
    BIND_CONSTANT(IMAGE_NORMAL);
    BIND_CONSTANT(IMAGE_ROUGHNESS);
    BIND_CONSTANT(IMAGE_METALLIC);
    BIND_CONSTANT(IMAGE_AO);
    BIND_CONSTANT(IMAGE_EMISSION);
    BIND_CONSTANT(ALPHA_OPAQUE);
    BIND_CONSTANT(ALPHA_CUTOUT);
    BIND_CONSTANT(ALPHA_BLEND);

    ADD_PROPERTY(PropertyInfo(Variant::VECTOR3, "size"), "", "get_size");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "units_per_meter"), "", "get_units_per_meter");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "fuel"), "", "get_fuel");
    ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "inputs"), "", "get_inputs");
    ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "skin_programs"), "", "get_skin_programs");
    ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "surfaces"), "", "get_surfaces");
    ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "textures"), "", "get_textures");
    ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "outputs"), "", "get_outputs");
    ADD_PROPERTY(PropertyInfo(Variant::VECTOR3, "bounds_min"), "", "get_bounds_min");
    ADD_PROPERTY(PropertyInfo(Variant::VECTOR3, "bounds_max"), "", "get_bounds_max");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "vertex_count"), "", "get_vertex_count");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "triangle_count"), "", "get_triangle_count");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "fuel_used"), "", "get_fuel_used");
    ADD_PROPERTY(PropertyInfo(Variant::DICTIONARY, "work"), "", "get_work");
    ADD_PROPERTY(PropertyInfo(Variant::DICTIONARY, "last_error"), "", "get_last_error");
}

IpropModel::IpropModel() = default;

IpropModel::~IpropModel() {
    iprop_parameters_free(parameters_);
    iprop3d_model_free(model_);
}

Error IpropModel::load_file(const String& path) {
    if (!FileAccess::file_exists(path)) {
        UtilityFunctions::printerr("iprop: no such file: ", path);
        return ERR_FILE_NOT_FOUND;
    }
    const PackedByteArray bytes = FileAccess::get_file_as_bytes(path);
    if (bytes.is_empty()) {
        UtilityFunctions::printerr("iprop: empty file: ", path);
        return ERR_INVALID_DATA;
    }
    Failure failure;
    iprop3d_model* loaded = nullptr;
    if (iprop3d_model_load(bytes.ptr(), static_cast<uint64_t>(bytes.size()), &loaded, failure.get()) != IPROP_EX_OK) {
        failure.print("model load");
        return ERR_INVALID_DATA;
    }
    std::unique_ptr<iprop3d_model, decltype(&iprop3d_model_free)> candidate(loaded, iprop3d_model_free);
    iprop3d_model_info info;
    if (iprop3d_model_info_init(&info, sizeof(info)) != IPROP_EX_OK ||
        iprop3d_model_query(candidate.get(), &info, failure.get()) != IPROP_EX_OK) {
        failure.print("model query");
        return ERR_INVALID_DATA;
    }
    Array programs;
    for (uint32_t i = 0; i < info.skin_program_count; ++i) {
        iprop3d_skin_program_info program;
        String name;
        if (iprop3d_skin_program_info_init(&program, sizeof(program)) != IPROP_EX_OK ||
            iprop3d_model_skin_program(candidate.get(), i, &program, failure.get()) != IPROP_EX_OK ||
            !decodeName(program.name, name)) {
            failure.print("model query");
            return ERR_INVALID_DATA;
        }
        Dictionary entry;
        entry["index"] = static_cast<int64_t>(program.program_index);
        entry["name"] = name;
        entry["width"] = static_cast<int64_t>(program.width);
        entry["height"] = static_cast<int64_t>(program.height);
        programs.append(entry);
    }
    iprop_parameters* parameters = nullptr;
    if (iprop3d_model_parameters_create(candidate.get(), &parameters, failure.get()) != IPROP_EX_OK) {
        failure.print("parameter setup");
        return ERR_INVALID_DATA;
    }
    iprop_parameters_free(parameters_);
    iprop3d_model_free(model_);
    parameters_ = parameters;
    model_ = candidate.release();
    size_ = Vector3(info.size_x, info.size_y, info.size_z);
    units_per_meter_ = info.units_per_meter;
    fuel_ = static_cast<int64_t>(info.fuel);
    skin_programs_ = programs;
    // Replace shared containers; results handed out earlier keep their data.
    surfaces_ = Array();
    textures_ = Array();
    bounds_min_ = bounds_max_ = Vector3();
    vertex_count_ = triangle_count_ = fuel_used_ = 0;
    work_ = Dictionary();
    last_error_ = Dictionary();
    emit_changed();
    return OK;
}

Array IpropModel::get_inputs() const {
    Array inputs;
    if (model_ == nullptr) return inputs;
    Failure failure;
    uint32_t count = 0;
    if (iprop3d_model_input_count(model_, &count, failure.get()) != IPROP_EX_OK) {
        failure.print("input query");
        return inputs;
    }
    for (uint32_t i = 0; i < count; ++i) {
        iprop_input_info_ex info;
        iprop_parameter_value_ex value;
        String name;
        if (iprop_input_info_ex_init(&info, sizeof(info)) != IPROP_EX_OK ||
            iprop_parameter_value_ex_init(&value, sizeof(value)) != IPROP_EX_OK ||
            iprop3d_model_input_get(model_, i, &info, failure.get()) != IPROP_EX_OK ||
            iprop_parameters_get(parameters_, info.input_index, &value, failure.get()) != IPROP_EX_OK ||
            !decodeName(info.name, name)) {
            failure.print("input query");
            return Array();
        }
        Dictionary entry;
        entry["index"] = static_cast<int64_t>(info.input_index);
        entry["name"] = name;
        entry["type"] = typeName(info.type);
        entry["default"] = inputValue(info.type, info.color_default, info.f32_default, info.u32_default);
        entry["value"] = inputValue(value.type, value.color, value.f32, value.u32);
        inputs.append(entry);
    }
    return inputs;
}

Error IpropModel::set_input(const String& name, const Variant& value) {
    const Error result = setNamedInput(parameters_, name, value);
    if (result == OK) emit_changed();
    return result;
}

Error IpropModel::reset_inputs() {
    if (parameters_ == nullptr) {
        UtilityFunctions::printerr("iprop: reset_inputs before load_file");
        return ERR_UNCONFIGURED;
    }
    Failure failure;
    if (iprop_parameters_reset(parameters_, failure.get()) != IPROP_EX_OK) {
        failure.print("reset_inputs");
        return FAILED;
    }
    emit_changed();
    return OK;
}

Ref<ArrayMesh> IpropModel::generate(int64_t seed, const Dictionary& limits) {
    last_error_ = Dictionary();
    // A cached texture that only the cache holds belongs to no mesh any more.
    for (auto entry = texture_cache_.begin(); entry != texture_cache_.end();) {
        if (entry->second.is_null() || entry->second->get_reference_count() <= 1) entry = texture_cache_.erase(entry);
        else ++entry;
    }
    const auto invalid = [&](const char* message) -> Ref<ArrayMesh> {
        last_error_["status"] = static_cast<int64_t>(IPROP_EX_E_ARGUMENT);
        last_error_["reason"] = static_cast<int64_t>(IPROP_REASON_INVALID_ARGUMENT);
        last_error_["stage"] = static_cast<int64_t>(IPROP_STAGE_ARGUMENTS);
        last_error_["location_flags"] = 0;
        last_error_["pc"] = 0;
        last_error_["message"] = message;
        UtilityFunctions::printerr("iprop: ", message);
        return {};
    };
    iprop_execution_limits_ex execution;
    Ref<IpropCancellation> cancellation;
    if (const char* problem = readLimits(limits, execution, cancellation)) return invalid(problem);
    if (seed < 0 || seed > std::numeric_limits<uint32_t>::max()) return invalid("seed must fit u32");
    if (model_ == nullptr) return invalid("generate before load_file");

    iprop3d_generate_options options;
    iprop3d_generate_options_init(&options, sizeof(options));
    options.seed = static_cast<uint32_t>(seed);
    Failure failure;
    iprop3d_result* result = nullptr;
    if (iprop3d_generate(model_, &options, parameters_, &execution, &result, failure.get()) != IPROP_EX_OK) {
        last_error_ = failure.details();
        failure.print("model generate");
        return {};
    }
    const std::unique_ptr<iprop3d_result, decltype(&iprop3d_result_free)> owned(result, iprop3d_result_free);
    const auto readFailure = [&]() -> Ref<ArrayMesh> {
        last_error_ = failure.details();
        if (failure.get()->status == IPROP_EX_OK) {
            last_error_["status"] = static_cast<int64_t>(IPROP_EX_E_INTERNAL);
            last_error_["reason"] = static_cast<int64_t>(IPROP_REASON_INTERNAL);
            last_error_["stage"] = static_cast<int64_t>(IPROP_STAGE_ARGUMENTS);
            last_error_["message"] = "unsupported result or allocation failure";
        }
        failure.print("model result read", "unsupported result or allocation failure");
        return {};
    };

    try {
    iprop3d_result_info info;
    if (iprop3d_result_info_init(&info, sizeof(info)) != IPROP_EX_OK ||
        iprop3d_result_query(result, &info, failure.get()) != IPROP_EX_OK)
        return readFailure();

    // Textures: the images as generated, and a GPU texture of each for the mesh.
    struct Gpu { Ref<ImageTexture> colour, maps[6]; bool repeats = false; };
    std::vector<Gpu> gpu(info.texture_count);
    Array candidate_textures;
    for (uint32_t t = 0; t < info.texture_count; ++t) {
        iprop3d_texture_view view;
        String name;
        if (iprop3d_texture_view_init(&view, sizeof(view)) != IPROP_EX_OK ||
            iprop3d_result_texture(result, t, &view, failure.get()) != IPROP_EX_OK ||
            view.pixel_format != IPROP_PIXEL_FORMAT_RGBA8 || !decodeName(view.name, name))
            return readFailure();
        const Ref<Image> colour = imageOf(view.width, view.height, view.byte_count, view.pixels);
        if (colour.is_null() || colour->is_empty()) return readFailure();
        char key[17];
        std::snprintf(key, sizeof(key), "%016llx", static_cast<unsigned long long>(view.key));
        Array images;
        Ref<Image> maps[6]; // by kind: normal, roughness, metallic, ao, emission
        for (uint32_t i = 0; i < view.image_count; ++i) {
            iprop_aux_image_view_ex aux;
            String aux_name;
            if (iprop_aux_image_view_ex_init(&aux, sizeof(aux)) != IPROP_EX_OK ||
                iprop3d_result_texture_image(result, t, i, &aux, failure.get()) != IPROP_EX_OK ||
                aux.pixel_format != IPROP_PIXEL_FORMAT_RGBA8 || !decodeName(aux.name, aux_name))
                return readFailure();
            const Ref<Image> image = imageOf(aux.width, aux.height, aux.byte_count, aux.pixels);
            if (image.is_null() || image->is_empty()) return readFailure();
            if (aux.kind >= IPROP_AUX_IMAGE_NORMAL && aux.kind <= IPROP_AUX_IMAGE_EMISSION && maps[aux.kind].is_null())
                maps[aux.kind] = image;
            Dictionary entry;
            entry["index"] = static_cast<int64_t>(aux.image_index);
            entry["kind"] = static_cast<int64_t>(aux.kind);
            entry["source"] = static_cast<int64_t>(aux.source);
            entry["name"] = aux_name;
            entry["image"] = image;
            images.append(entry);
        }
        Dictionary entry;
        entry["index"] = static_cast<int64_t>(t);
        entry["key"] = String(key);
        entry["name"] = name;
        entry["width"] = static_cast<int64_t>(view.width);
        entry["height"] = static_cast<int64_t>(view.height);
        entry["asset_width"] = static_cast<int64_t>(view.asset_width);
        entry["asset_height"] = static_cast<int64_t>(view.asset_height);
        entry["repeats"] = view.repeats != 0;
        entry["image"] = colour;
        entry["images"] = images;
        entry["normal"] = maps[IPROP_AUX_IMAGE_NORMAL];
        candidate_textures.append(entry);
        const auto upload = [&](const Ref<Image>& image, uint32_t kind) -> Ref<ImageTexture> {
            if (image.is_null()) return Ref<ImageTexture>();
            const std::string cached = std::string(key) + ":" + std::to_string(kind);
            if (const auto found = texture_cache_.find(cached); found != texture_cache_.end()) return found->second;
            const Ref<ImageTexture> texture = textureOf(image, kind == IPROP_AUX_IMAGE_NORMAL);
            if (texture.is_valid()) texture_cache_[cached] = texture;
            return texture;
        };
        gpu[t].colour = upload(colour, 0);
        for (uint32_t kind = IPROP_AUX_IMAGE_NORMAL; kind <= IPROP_AUX_IMAGE_EMISSION; ++kind)
            gpu[t].maps[kind] = upload(maps[kind], kind);
        gpu[t].repeats = view.repeats != 0;
        if (gpu[t].colour.is_null()) return readFailure();
    }

    // Surfaces: the raw arrays, and the mesh adapted to Godot.
    const float units = info.units_per_meter;
    Ref<ArrayMesh> mesh;
    mesh.instantiate();
    Array candidate_surfaces;
    for (uint32_t s = 0; s < info.surface_count; ++s) {
        iprop3d_surface_view view;
        if (iprop3d_surface_view_init(&view, sizeof(view)) != IPROP_EX_OK ||
            iprop3d_result_surface(result, s, &view, failure.get()) != IPROP_EX_OK ||
            view.texture_index >= info.texture_count || view.index_count % 3 != 0)
            return readFailure();
        const size_t vertices = view.vertex_count, indices = view.index_count;
        PackedFloat32Array positions, normals, tangents, uvs;
        PackedInt32Array raw_indices;
        static_assert(sizeof(int32_t) == sizeof(uint32_t));
        if (!copyInto(positions, view.positions, vertices * 3) || !copyInto(normals, view.normals, vertices * 3) ||
            !copyInto(tangents, view.tangents, vertices * 4) || !copyInto(uvs, view.uvs, vertices * 2) ||
            !copyInto(raw_indices, reinterpret_cast<const int32_t*>(view.indices), indices))
            return readFailure();
        Dictionary entry;
        entry["index"] = static_cast<int64_t>(s);
        entry["texture"] = static_cast<int64_t>(view.texture_index);
        entry["roughness"] = view.roughness;
        entry["metallic"] = view.metallic;
        entry["emission"] = view.emission;
        entry["alpha_mode"] = static_cast<int64_t>(view.alpha_mode);
        entry["alpha_cutoff"] = view.alpha_cutoff;
        entry["two_sided"] = view.two_sided != 0;
        entry["crisp"] = view.crisp != 0;
        entry["vertex_count"] = static_cast<int64_t>(view.vertex_count);
        entry["index_count"] = static_cast<int64_t>(view.index_count);
        entry["positions"] = positions;
        entry["normals"] = normals;
        entry["tangents"] = tangents;
        entry["uvs"] = uvs;
        entry["indices"] = raw_indices;
        candidate_surfaces.append(entry);

        // The mesh: metres, and clockwise front faces as Godot draws them.
        PackedVector3Array mesh_positions, mesh_normals;
        PackedVector2Array mesh_uvs;
        PackedInt32Array mesh_indices;
        if (mesh_positions.resize(static_cast<int64_t>(vertices)) != OK || mesh_normals.resize(static_cast<int64_t>(vertices)) != OK ||
            mesh_uvs.resize(static_cast<int64_t>(vertices)) != OK || mesh_indices.resize(static_cast<int64_t>(indices)) != OK)
            return readFailure();
        Vector3* out_positions = mesh_positions.ptrw();
        Vector3* out_normals = mesh_normals.ptrw();
        Vector2* out_uvs = mesh_uvs.ptrw();
        for (size_t i = 0; i < vertices; ++i) {
            out_positions[i] = Vector3(view.positions[i * 3] / units, view.positions[i * 3 + 1] / units, view.positions[i * 3 + 2] / units);
            out_normals[i] = Vector3(view.normals[i * 3], view.normals[i * 3 + 1], view.normals[i * 3 + 2]);
            out_uvs[i] = Vector2(view.uvs[i * 2], view.uvs[i * 2 + 1]);
        }
        int32_t* out_indices = mesh_indices.ptrw();
        for (size_t i = 0; i < indices; i += 3) {
            out_indices[i] = static_cast<int32_t>(view.indices[i]);
            out_indices[i + 1] = static_cast<int32_t>(view.indices[i + 2]);
            out_indices[i + 2] = static_cast<int32_t>(view.indices[i + 1]);
        }
        Array arrays;
        arrays.resize(Mesh::ARRAY_MAX);
        arrays[Mesh::ARRAY_VERTEX] = mesh_positions;
        arrays[Mesh::ARRAY_NORMAL] = mesh_normals;
        arrays[Mesh::ARRAY_TANGENT] = tangents;
        arrays[Mesh::ARRAY_TEX_UV] = mesh_uvs;
        arrays[Mesh::ARRAY_INDEX] = mesh_indices;
        mesh->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, arrays);

        const Gpu& texture = gpu[view.texture_index];
        Ref<StandardMaterial3D> material;
        material.instantiate();
        material->set_texture(BaseMaterial3D::TEXTURE_ALBEDO, texture.colour);
        if (texture.maps[IPROP_AUX_IMAGE_NORMAL].is_valid()) {
            material->set_feature(BaseMaterial3D::FEATURE_NORMAL_MAPPING, true);
            material->set_texture(BaseMaterial3D::TEXTURE_NORMAL, texture.maps[IPROP_AUX_IMAGE_NORMAL]);
        }
        // The numbers multiply the maps (material maps v1.2); a scalar map
        // holds its value in red, green and blue alike.
        material->set_roughness(view.roughness);
        material->set_metallic(view.metallic);
        if (texture.maps[IPROP_AUX_IMAGE_ROUGHNESS].is_valid()) {
            material->set_texture(BaseMaterial3D::TEXTURE_ROUGHNESS, texture.maps[IPROP_AUX_IMAGE_ROUGHNESS]);
            material->set_roughness_texture_channel(BaseMaterial3D::TEXTURE_CHANNEL_RED);
        }
        if (texture.maps[IPROP_AUX_IMAGE_METALLIC].is_valid()) {
            material->set_texture(BaseMaterial3D::TEXTURE_METALLIC, texture.maps[IPROP_AUX_IMAGE_METALLIC]);
            material->set_metallic_texture_channel(BaseMaterial3D::TEXTURE_CHANNEL_RED);
        }
        if (texture.maps[IPROP_AUX_IMAGE_AO].is_valid()) {
            material->set_feature(BaseMaterial3D::FEATURE_AMBIENT_OCCLUSION, true);
            material->set_texture(BaseMaterial3D::TEXTURE_AMBIENT_OCCLUSION, texture.maps[IPROP_AUX_IMAGE_AO]);
            material->set_ao_texture_channel(BaseMaterial3D::TEXTURE_CHANNEL_RED);
        }
        // Crisp: every texel a hard square, as pixel art wants it.
        material->set_texture_filter(view.crisp != 0 ? BaseMaterial3D::TEXTURE_FILTER_NEAREST
                                                     : BaseMaterial3D::TEXTURE_FILTER_LINEAR_WITH_MIPMAPS_ANISOTROPIC);
        material->set_flag(BaseMaterial3D::FLAG_USE_TEXTURE_REPEAT, texture.repeats);
        // An opaque surface ignores the image's alpha; a cutout one tests it.
        if (view.alpha_mode == IPROP3D_ALPHA_CUTOUT) {
            material->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA_SCISSOR);
            material->set_alpha_scissor_threshold(view.alpha_cutoff);
        }
        // Glass mixes over what is behind it and writes no depth.
        if (view.alpha_mode == IPROP3D_ALPHA_BLEND) material->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA);
        if (view.two_sided != 0) material->set_cull_mode(BaseMaterial3D::CULL_DISABLED);
        if (view.emission > 0.f) {
            // The emission map glows, else the colour image: white times the
            // texture, times the amount.
            material->set_feature(BaseMaterial3D::FEATURE_EMISSION, true);
            material->set_emission(Color(1, 1, 1));
            material->set_emission_operator(BaseMaterial3D::EMISSION_OP_MULTIPLY);
            material->set_texture(BaseMaterial3D::TEXTURE_EMISSION, texture.maps[IPROP_AUX_IMAGE_EMISSION].is_valid()
                                                                      ? texture.maps[IPROP_AUX_IMAGE_EMISSION] : texture.colour);
            material->set_emission_energy_multiplier(view.emission);
        }
        const int surface = mesh->get_surface_count() - 1;
        if (surface != static_cast<int>(s)) return readFailure();
        mesh->surface_set_material(surface, material);
        mesh->surface_set_name(surface, Dictionary(candidate_textures[view.texture_index])["name"]);
    }

    // Outputs (M3): points and boxes in metres, in Godot's axes (the same
    // right-handed +Y up as the model's), divided as the mesh's positions are
    // so that a point on a vertex stays on it.
    Array candidate_outputs;
    for (uint32_t o = 0; o < info.output_count; ++o) {
        iprop3d_output_view view;
        String name;
        if (iprop3d_output_view_init(&view, sizeof(view)) != IPROP_EX_OK ||
            iprop3d_result_output(result, o, &view, failure.get()) != IPROP_EX_OK || !decodeName(view.name, name))
            return readFailure();
        Dictionary entry;
        entry["index"] = static_cast<int64_t>(o);
        entry["name"] = name;
        entry["list"] = view.list != 0;
        const float* v = view.values;
        const auto point = [&](uint32_t at) { return Vector3(v[at] / units, v[at + 1] / units, v[at + 2] / units); };
        if (view.kind == IPROP3D_OUTPUT_BOX3D) {
            entry["kind"] = "box3d";
            Array boxes;
            for (uint32_t i = 0; i < view.value_count; ++i) {
                const uint32_t at = i * 15;
                Dictionary box;
                // Basis(x, y, z) takes the axes as its columns.
                const Basis basis(Vector3(v[at + 6], v[at + 7], v[at + 8]), Vector3(v[at + 9], v[at + 10], v[at + 11]),
                                  Vector3(v[at + 12], v[at + 13], v[at + 14]));
                box["transform"] = Transform3D(basis, point(at));
                box["size"] = point(at + 3);
                boxes.push_back(box);
            }
            entry["boxes"] = boxes;
        } else {
            entry["kind"] = "point3d";
            PackedVector3Array points;
            for (uint32_t i = 0; i < view.value_count; ++i) points.push_back(point(i * 3));
            entry["points"] = points;
        }
        candidate_outputs.push_back(entry);
    }

    Dictionary candidate_work;
    candidate_work["budget"] = static_cast<int64_t>(info.work_budget);
    candidate_work["used"] = static_cast<int64_t>(info.work_used);
    surfaces_ = candidate_surfaces;
    textures_ = candidate_textures;
    outputs_ = candidate_outputs;
    bounds_min_ = Vector3(info.bounds_min[0], info.bounds_min[1], info.bounds_min[2]);
    bounds_max_ = Vector3(info.bounds_max[0], info.bounds_max[1], info.bounds_max[2]);
    vertex_count_ = info.vertex_count;
    triangle_count_ = info.triangle_count;
    fuel_used_ = static_cast<int64_t>(info.fuel_used);
    work_ = candidate_work;
    return mesh;
    } catch (const std::exception&) { return readFailure(); }
}

// ---- IpropModelInstance3D -------------------------------------------------

void IpropModelInstance3D::_bind_methods() {
    ClassDB::bind_method(D_METHOD("refresh"), &IpropModelInstance3D::refresh);
    ClassDB::bind_method(D_METHOD("set_model", "model"), &IpropModelInstance3D::set_model);
    ClassDB::bind_method(D_METHOD("get_model"), &IpropModelInstance3D::get_model);
    ClassDB::bind_method(D_METHOD("set_seed", "seed"), &IpropModelInstance3D::set_seed);
    ClassDB::bind_method(D_METHOD("get_seed"), &IpropModelInstance3D::get_seed);
    ClassDB::bind_method(D_METHOD("set_markers", "enabled"), &IpropModelInstance3D::set_markers);
    ClassDB::bind_method(D_METHOD("get_markers"), &IpropModelInstance3D::get_markers);
    ClassDB::bind_method(D_METHOD("set_colliders", "enabled"), &IpropModelInstance3D::set_colliders);
    ClassDB::bind_method(D_METHOD("get_colliders"), &IpropModelInstance3D::get_colliders);

    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "model", PROPERTY_HINT_RESOURCE_TYPE, "IpropModel"),
                 "set_model", "get_model");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "seed"), "set_seed", "get_seed");
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "markers"), "set_markers", "get_markers");
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "colliders"), "set_colliders", "get_colliders");
}

void IpropModelInstance3D::_ready() {
    refresh();
}

void IpropModelInstance3D::refresh() {
    if (model_.is_null() || !model_->is_loaded()) {
        set_mesh(Ref<Mesh>());
        rebuild_outputs();
        return;
    }
    // A failed generation returns null: the node then shows nothing.
    const Ref<ArrayMesh> mesh = model_->generate(seed_);
    set_mesh(mesh);
    rebuild_outputs();
}

// The children of the outputs: removed and made again for each mesh.
void IpropModelInstance3D::rebuild_outputs() {
    for (const ObjectID id : output_nodes_) {
        if (Node* node = Object::cast_to<Node>(ObjectDB::get_instance(id))) {
            remove_child(node);
            node->queue_free();
        }
    }
    output_nodes_.clear();
    if (model_.is_null() || get_mesh().is_null()) return;
    const Array outputs = model_->get_outputs();
    StaticBody3D* body = nullptr;
    for (int64_t o = 0; o < outputs.size(); ++o) {
        const Dictionary output = outputs[o];
        const String name = output["name"];
        const bool list = output["list"];
        if (String(output["kind"]) == "point3d") {
            if (!markers_) continue;
            const PackedVector3Array points = output["points"];
            for (int64_t i = 0; i < points.size(); ++i) {
                Marker3D* marker = memnew(Marker3D);
                marker->set_name(list ? name + String("_") + String::num_int64(i) : name);
                marker->set_position(points[i]);
                add_child(marker);
                output_nodes_.push_back(ObjectID(marker->get_instance_id()));
            }
            continue;
        }
        if (!colliders_) continue;
        const Array boxes = output["boxes"];
        for (int64_t i = 0; i < boxes.size(); ++i) {
            if (body == nullptr) {
                body = memnew(StaticBody3D);
                body->set_name("Colliders");
                add_child(body);
                output_nodes_.push_back(ObjectID(body->get_instance_id()));
            }
            const Dictionary box = boxes[i];
            Ref<BoxShape3D> shape;
            shape.instantiate();
            shape->set_size(box["size"]);
            CollisionShape3D* collider = memnew(CollisionShape3D);
            collider->set_name(list ? name + String("_") + String::num_int64(i) : name);
            collider->set_shape(shape);
            collider->set_transform(box["transform"]);
            body->add_child(collider);
        }
    }
}

void IpropModelInstance3D::set_markers(bool enabled) {
    markers_ = enabled;
    rebuild_outputs();
}

void IpropModelInstance3D::set_colliders(bool enabled) {
    colliders_ = enabled;
    rebuild_outputs();
}

void IpropModelInstance3D::set_model(const Ref<IpropModel>& model) {
    const Callable changed(this, "refresh");
    if (model_.is_valid() && model_->is_connected("changed", changed)) {
        model_->disconnect("changed", changed);
    }
    model_ = model;
    if (model_.is_valid()) {
        model_->connect("changed", changed);
    }
    refresh();
}

void IpropModelInstance3D::set_seed(int64_t seed) {
    seed_ = seed;
    refresh();
}

} // namespace iprop_godot
