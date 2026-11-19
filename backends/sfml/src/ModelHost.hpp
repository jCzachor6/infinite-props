#pragma once

#include "Host.hpp"
#include "iprop3d.h"

#include <map>
#include <mutex>
#include <span>

namespace iprop::sfml_host {
using Model = std::unique_ptr<iprop3d_model, decltype(&iprop3d_model_free)>;
using ModelResult = std::unique_ptr<iprop3d_result, decltype(&iprop3d_result_free)>;

inline iprop3d_library_info modelLibrary() {
    Error error;
    iprop3d_library_info info{};
    info.struct_size = sizeof(info);
    error.check(iprop3d_library_query(&info, error.get()), "model library query");
    if (info.api_major != 0 || info.api_minor != 3) throw std::runtime_error("Expected model API 0.3");
    iprop_library_info_ex flat{};
    flat.struct_size = sizeof(flat);
    error.check(iprop_library_query_ex(&flat, error.get()), "2D library query");
    constexpr auto required = IPROP_CAP_PART_OUTPUTS | IPROP_CAP_RESULT_RESOURCES | IPROP_CAP_EXECUTION_LIMITS
                            | IPROP_CAP_AUX_IMAGES | IPROP_CAP_DERIVED_NORMAL;
    if (flat.api_major != 0 || flat.api_minor < 7 || flat.abi_major != 0 || flat.abi_minor != 5 ||
        (flat.capabilities & required) != required) throw std::runtime_error("Incompatible 2D library");
    return info;
}

inline iprop3d_model* loadModel(const std::vector<std::uint8_t>& data) {
    modelLibrary();
    Error error;
    iprop3d_model* model = nullptr;
    const std::uint8_t empty = 0;
    error.check(iprop3d_model_load(data.empty() ? &empty : data.data(), data.size(), &model, error.get()), "load model");
    return model;
}

inline iprop3d_model_info queryModel(const iprop3d_model* model) {
    Error error;
    iprop3d_model_info info{};
    info.struct_size = sizeof(info);
    error.check(iprop3d_model_query(model, &info, error.get()), "query model");
    return info;
}

inline std::uint64_t textureStamp() { return iprop3d_texture_stamp(); }

struct ModelSkinProgram {
    std::uint32_t index, width, height;
    std::string name;
};

inline std::vector<ModelSkinProgram> modelSkinPrograms(const iprop3d_model* model) {
    auto count = queryModel(model).skin_program_count;
    std::vector<ModelSkinProgram> skins;
    Error error;
    for (std::uint32_t i = 0; i < count; ++i) {
        iprop3d_skin_program_info v{};
        v.struct_size = sizeof(v);
        error.check(iprop3d_model_skin_program(model, i, &v, error.get()), "model skin program");
        skins.push_back({i, v.width, v.height, v.name.byte_count ? std::string(v.name.data, v.name.byte_count) : ""});
    }
    return skins;
}

struct ModelInput {
    std::string name;
    std::uint32_t index, type;
    iprop_color_rgba8 colorDefault;
    float floatDefault;
    std::uint32_t uintDefault;
};

inline std::vector<ModelInput> modelInputs(const iprop3d_model* model) {
    auto count = queryModel(model).input_count;
    std::vector<ModelInput> inputs;
    Error error;
    for (std::uint32_t i = 0; i < count; ++i) {
        iprop_input_info_ex v{};
        v.struct_size = sizeof(v);
        error.check(iprop3d_model_input_get(model, i, &v, error.get()), "model input");
        inputs.push_back({v.name.byte_count ? std::string(v.name.data, v.name.byte_count) : "",
            i, v.type, v.color_default, v.f32_default, v.u32_default});
    }
    return inputs;
}

inline Parameters createModelParameters(const iprop3d_model* model) {
    Error error;
    iprop_parameters* parameters = nullptr;
    error.check(iprop3d_model_parameters_create(model, &parameters, error.get()), "create model parameters");
    return Parameters(parameters, iprop_parameters_free);
}

struct ModelRequest {
    std::uint32_t seed = 0, lod = 0;
    ExecutionLimits limits;
    std::span<const iprop3d_known_texture> known;
};

inline iprop3d_result* generateModel(const iprop3d_model* model, const ModelRequest& request,
                                     const iprop_parameters* parameters = nullptr) {
    Error error;
    iprop3d_generate_options options{};
    options.struct_size = sizeof(options);
    options.seed = request.seed;
    options.lod = request.lod;
    if (request.known.size() > UINT32_MAX) throw std::length_error("Too many known textures");
    options.known_textures = request.known.data();
    options.known_texture_count = static_cast<std::uint32_t>(request.known.size());
    iprop_execution_limits_ex limits{};
    limits.struct_size = sizeof(limits);
    limits.work_limit = request.limits.workLimit;
    limits.timeout_ns = request.limits.timeoutNanoseconds;
    limits.cancellation = request.limits.cancellation.get();
    iprop3d_result* result = nullptr;
    error.check(iprop3d_generate(model, &options, parameters, &limits, &result, error.get()), "generate model");
    return result;
}

struct Surface {
    std::uint32_t index, textureIndex;
    float roughness, metallic, emission, alphaCutoff;
    iprop3d_alpha_mode alphaMode;
    bool twoSided, crisp;
    std::vector<float> positions, normals, tangents, uvs;
    std::vector<std::uint32_t> indices;
};

struct TextureImages {
    std::vector<std::uint8_t> rgba;
    std::vector<AuxiliaryImage> images;
};

struct ModelTexture {
    iprop3d_known_texture known;
    std::string name;
    std::uint32_t index, width, height, assetWidth, assetHeight;
    std::uint64_t rowStride;
    std::uint32_t pixelFormat, alphaMode, origin;
    bool repeats, reused;
    std::shared_ptr<const TextureImages> data;

    std::string keyHex() const {
        char hex[17];
        std::snprintf(hex, sizeof(hex), "%016llx", static_cast<unsigned long long>(known.key));
        return hex;
    }
};

struct ModelOutput {
    std::string name;
    std::uint32_t index, kind, valueCount, floatsPerValue;
    bool isList;
    std::vector<float> values;
};

struct GeneratedModel {
    iprop3d_result_info statistics{};
    std::vector<Surface> surfaces;
    std::vector<ModelTexture> textures;
    std::vector<ModelOutput> outputs;
};

inline std::string modelName(iprop_utf8_view view) {
    return view.byte_count ? std::string(view.data, view.byte_count) : std::string();
}

template<class T> std::vector<T> copyModelArray(const T* values, std::size_t count) {
    if (!count) return {};
    return {values, values + count};
}

inline GeneratedModel copyModel(const iprop3d_result* result) {
    Error error;
    GeneratedModel out;
    auto& info = out.statistics;
    info.struct_size = sizeof(info);
    error.check(iprop3d_result_query(result, &info, error.get()), "query model result");
    for (std::uint32_t i = 0; i < info.surface_count; ++i) {
        iprop3d_surface_view s{};
        s.struct_size = sizeof(s);
        error.check(iprop3d_result_surface(result, i, &s, error.get()), "read model surface");
        out.surfaces.push_back({i, s.texture_index, s.roughness, s.metallic, s.emission, s.alpha_cutoff,
            s.alpha_mode, s.two_sided != 0, s.crisp != 0, copyModelArray(s.positions, 3*s.vertex_count),
            copyModelArray(s.normals, 3*s.vertex_count), copyModelArray(s.tangents, 4*s.vertex_count),
            copyModelArray(s.uvs, 2*s.vertex_count), copyModelArray(s.indices, s.index_count)});
    }
    for (std::uint32_t i = 0; i < info.texture_count; ++i) {
        iprop3d_texture_view t{};
        t.struct_size = sizeof(t);
        error.check(iprop3d_result_texture(result, i, &t, error.get()), "read model texture");
        auto data = std::make_shared<TextureImages>();
        data->rgba = copyModelArray(t.pixels, static_cast<std::size_t>(t.byte_count));
        for (std::uint32_t j = 0; j < t.image_count; ++j) {
            AuxiliaryImage a{};
            a.info.struct_size = sizeof(a.info);
            error.check(iprop3d_result_texture_image(result, i, j, &a.info, error.get()), "read model texture map");
            a.name = modelName(a.info.name);
            a.pixels = copyModelArray(a.info.pixels, static_cast<std::size_t>(a.info.byte_count));
            a.info.pixels = nullptr;
            a.info.name = {};
            data->images.push_back(std::move(a));
        }
        out.textures.push_back({{t.key, t.fuel, t.work, t.image_kinds, 0}, modelName(t.name), i,
            t.width, t.height, t.asset_width, t.asset_height, t.row_stride, t.pixel_format,
            t.alpha_mode, t.origin, t.repeats != 0, t.reused != 0, std::move(data)});
    }
    for (std::uint32_t i = 0; i < info.output_count; ++i) {
        iprop3d_output_view v{};
        v.struct_size = sizeof(v);
        error.check(iprop3d_result_output(result, i, &v, error.get()), "read model output");
        out.outputs.push_back({modelName(v.name), i, v.kind, v.value_count, v.floats_per_value,
            v.list != 0, copyModelArray(v.values, static_cast<std::size_t>(v.value_count) * v.floats_per_value)});
    }
    return out;
}

class TextureCache {
public:
    using Entries = std::map<std::uint64_t, ModelTexture>;
    Entries snapshot() const {
        std::lock_guard lock(mutex_);
        return entries_;
    }
    std::vector<iprop3d_known_texture> known() const {
        std::lock_guard lock(mutex_);
        return records(entries_);
    }
    void complete(GeneratedModel& result) { complete(result, snapshot()); }
    GeneratedModel generate(const iprop3d_model* model, ModelRequest request,
                            const iprop_parameters* parameters = nullptr) {
        auto held = snapshot();
        auto known = records(held);
        known.insert(known.end(), request.known.begin(), request.known.end());
        request.known = known;
        ModelResult result(generateModel(model, request, parameters), iprop3d_result_free);
        auto out = copyModel(result.get());
        complete(out, held);
        return out;
    }
    void clear() { std::lock_guard lock(mutex_); entries_.clear(); }
    void remove(std::uint64_t key) { std::lock_guard lock(mutex_); entries_.erase(key); }
private:
    static std::vector<iprop3d_known_texture> records(const Entries& entries) {
        std::vector<iprop3d_known_texture> result;
        result.reserve(entries.size());
        for (const auto& entry : entries) result.push_back(entry.second.known);
        return result;
    }
    void complete(GeneratedModel& out, const Entries& held) {
        for (auto& texture : out.textures) {
            if (texture.reused) {
                auto entry = held.find(texture.known.key);
                if (entry == held.end()) throw std::out_of_range("Missing model texture in cache");
                const auto& stored = entry->second;
                texture.data = stored.data;
                texture.rowStride = stored.rowStride;
                texture.pixelFormat = stored.pixelFormat;
                texture.alphaMode = stored.alphaMode;
                texture.origin = stored.origin;
            }
        }
        std::lock_guard lock(mutex_);
        for (const auto& t : out.textures) if (!t.reused) entries_[t.known.key] = t;
    }
    mutable std::mutex mutex_;
    Entries entries_;
};
} // namespace iprop::sfml_host
