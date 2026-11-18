// The model entries of the WASM module (preview): a model is loaded and
// validated once and generated many times, through the same checked C API
// as the native hosts (backends/capi3d). Everything a call hands to JS is
// an owned copy; nothing borrows the WASM heap. module_api3d.js turns a
// returned error into a thrown JS Error after the C++ frames have unwound.
//
// This file is linked together with bindings.cpp: the model module is a
// superset that also carries the 2D `generate`.
#include <emscripten/bind.h>
#include <emscripten/val.h>
#include <emscripten.h>

#include "iprop3d.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

using emscripten::val;
using emscripten::EM_VAL;
using u32 = std::uint32_t;

// The cancellation sources of a generation, defined by bindings.cpp.
extern "C" u32 iprop_cancel_js(EM_VAL handle);

namespace {

// The error conventions of bindings.cpp, which keeps its own copies private.
class ApiError : public std::runtime_error {
public:
    iprop_error_ex detail;
    ApiError(const iprop_error_ex& error, const char* message) : std::runtime_error(message), detail(error) {
        detail.message_buffer = nullptr;
    }
};
struct Checked {
    iprop_error_ex error{};
    char message[8192]{};
    Checked() {
        iprop_error_ex_init(&error, sizeof(error));
        error.message_buffer = message; error.message_capacity = sizeof(message);
    }
    void operator()(iprop_status_ex status) const { if (status != IPROP_EX_OK) throw ApiError(error, message); }
};
val errorResult(const std::string& message, const iprop_error_ex* detail = nullptr) {
    auto error = val::global("Error").new_(val("iprop: " + message));
    if (detail) {
        error.set("status", detail->status); error.set("reason", detail->reason); error.set("stage", detail->stage);
        error.set("byteOffset", static_cast<double>(detail->byte_offset)); error.set("locationFlags", detail->location_flags);
        error.set("offsetSpace", detail->offset_space); error.set("functionIndex", detail->function_index);
        error.set("pc", detail->pc); error.set("debugLine", detail->debug_line);
        error.set("messageTruncated", detail->message_truncated != 0);
    }
    auto result = val::object(); result.set("__ipropError", error); return result;
}

using Model = std::unique_ptr<iprop3d_model, decltype(&iprop3d_model_free)>;
// Loaded models by the handle JS holds. A handle is never reused, so a stale
// one is an ordinary error.
std::map<u32, Model> g_models;
u32 g_nextModel = 1;

std::string text(iprop_utf8_view view) { return view.byte_count ? std::string(view.data, view.byte_count) : std::string(); }

val triple(const float* values) {
    auto list = val::array();
    for (int axis = 0; axis < 3; ++axis) list.call<void>("push", values[axis]);
    return list;
}

// An owned JS typed array with the contents of a borrowed C array.
template<class T> val copied(const T* data, std::size_t count) {
    return val(emscripten::typed_memory_view(count, data)).call<val>("slice");
}

bool hexDigit(char c, std::uint8_t& out) {
    if (c >= '0' && c <= '9') { out = static_cast<std::uint8_t>(c - '0'); return true; }
    if (c >= 'a' && c <= 'f') { out = static_cast<std::uint8_t>(c - 'a' + 10); return true; }
    if (c >= 'A' && c <= 'F') { out = static_cast<std::uint8_t>(c - 'A' + 10); return true; }
    return false;
}
bool parseColor(const std::string& value, iprop_color_rgba8& color) {
    if ((value.size() != 7 && value.size() != 9) || value[0] != '#') return false;
    std::uint8_t* channels[4] = {&color.r, &color.g, &color.b, &color.a}; color.a = 255;
    for (std::size_t i = 0; i < (value.size() - 1) / 2; ++i) {
        std::uint8_t hi = 0, lo = 0;
        if (!hexDigit(value[1 + i * 2], hi) || !hexDigit(value[2 + i * 2], lo)) return false;
        *channels[i] = static_cast<std::uint8_t>(hi * 16u + lo);
    }
    return true;
}

// `uniforms` by input name, with the value rules of the 2D `generate`:
// numbers for f32 and u32, "#RRGGBB" or "#RRGGBBAA" for a colour.
void readUniforms(const iprop3d_model* model, iprop_parameters* parameters, val uniforms, Checked& check) {
    if (uniforms.isUndefined() || uniforms.isNull()) return;
    const auto names = emscripten::vecFromJSArray<std::string>(val::global("Object").call<val>("keys", uniforms));
    for (const auto& name : names) {
        u32 index = 0;
        const iprop_utf8_view key{name.data(), static_cast<u32>(name.size())};
        if (iprop3d_model_input_find(model, key, &index, &check.error) != IPROP_EX_OK)
            throw std::invalid_argument("unknown uniform '" + name + "'");
        iprop_input_info_ex input{}; iprop_input_info_ex_init(&input, sizeof(input));
        check(iprop3d_model_input_get(model, index, &input, &check.error));
        const auto value = uniforms[name];
        if (input.type == IPROP_INPUT_COLOR) {
            iprop_color_rgba8 color{};
            if (!value.isString() || !parseColor(value.as<std::string>(), color))
                throw std::invalid_argument("uniform '" + name + "' must be a #RRGGBB or #RRGGBBAA colour");
            check(iprop_parameters_set_color(parameters, index, color, &check.error));
        } else if (input.type == IPROP_INPUT_F32) {
            if (!value.isNumber()) throw std::invalid_argument("uniform '" + name + "' must be a number");
            check(iprop_parameters_set_f32(parameters, index, static_cast<float>(value.as<double>()), &check.error));
        } else {
            const double number = value.isNumber() ? value.as<double>() : -1.0;
            if (!(number >= 0.0 && number <= 4294967295.0) || number != static_cast<double>(static_cast<u32>(number)))
                throw std::invalid_argument("uniform '" + name + "' must be an unsigned 32-bit integer");
            check(iprop_parameters_set_u32(parameters, index, static_cast<u32>(number), &check.error));
        }
    }
}

// What a model declares, read once when it is loaded.
val describe(const iprop3d_model* model, Checked& check) {
    iprop3d_model_info info{}; iprop3d_model_info_init(&info, sizeof(info));
    check(iprop3d_model_query(model, &info, &check.error));
    auto object = val::object();
    const float size[3] = {info.size_x, info.size_y, info.size_z};
    object.set("size", triple(size)); object.set("unitsPerMeter", info.units_per_meter);
    object.set("fuel", static_cast<double>(info.fuel)); object.set("containerFlags", info.container_flags);
    auto inputs = val::array();
    for (u32 i = 0; i < info.input_count; ++i) {
        iprop_input_info_ex input{}; iprop_input_info_ex_init(&input, sizeof(input));
        check(iprop3d_model_input_get(model, i, &input, &check.error));
        auto entry = val::object();
        entry.set("index", i); entry.set("name", text(input.name));
        if (input.type == IPROP_INPUT_COLOR) {
            char hex[16];
            std::snprintf(hex, sizeof(hex), "#%02x%02x%02x%02x", input.color_default.r, input.color_default.g,
                          input.color_default.b, input.color_default.a);
            entry.set("type", std::string("color")); entry.set("default", std::string(hex));
        } else if (input.type == IPROP_INPUT_F32) {
            entry.set("type", std::string("f32")); entry.set("default", input.f32_default);
        } else {
            entry.set("type", std::string("u32")); entry.set("default", input.u32_default);
        }
        inputs.call<void>("push", entry);
    }
    object.set("inputs", inputs);
    auto programs = val::array();
    for (u32 i = 0; i < info.skin_program_count; ++i) {
        iprop3d_skin_program_info program{}; iprop3d_skin_program_info_init(&program, sizeof(program));
        check(iprop3d_model_skin_program(model, i, &program, &check.error));
        auto entry = val::object();
        entry.set("index", i); entry.set("name", text(program.name));
        entry.set("width", program.width); entry.set("height", program.height);
        programs.call<void>("push", entry);
    }
    object.set("skinPrograms", programs);
    return object;
}

val textures(const iprop3d_result* result, u32 count, Checked& check) {
    auto list = val::array();
    for (u32 t = 0; t < count; ++t) {
        iprop3d_texture_view view{}; iprop3d_texture_view_init(&view, sizeof(view));
        check(iprop3d_result_texture(result, t, &view, &check.error));
        auto entry = val::object();
        // A JS number cannot hold 64 bits exactly: the key is sixteen hex digits.
        char key[17];
        std::snprintf(key, sizeof(key), "%016llx", static_cast<unsigned long long>(view.key));
        entry.set("index", t); entry.set("key", std::string(key)); entry.set("name", text(view.name));
        entry.set("width", view.width); entry.set("height", view.height);
        entry.set("assetWidth", view.asset_width); entry.set("assetHeight", view.asset_height);
        entry.set("repeats", view.repeats != 0);
        entry.set("rowStride", static_cast<double>(view.row_stride)); entry.set("pixelFormat", view.pixel_format);
        entry.set("alphaMode", view.alpha_mode); entry.set("storageOrigin", view.origin);
        entry.set("data", copied(view.pixels, static_cast<std::size_t>(view.byte_count)));
        // What it cost and what it carries (API 0.3): the record of a known
        // texture. A reused one has no pixels; the page holds them.
        entry.set("fuel", static_cast<double>(view.fuel)); entry.set("work", static_cast<double>(view.work));
        entry.set("kinds", view.image_kinds); entry.set("reused", view.reused != 0);
        auto images = val::array();
        for (u32 i = 0; i < view.image_count; ++i) {
            iprop_aux_image_view_ex image{}; iprop_aux_image_view_ex_init(&image, sizeof(image));
            check(iprop3d_result_texture_image(result, t, i, &image, &check.error));
            auto aux = val::object();
            aux.set("index", i); aux.set("kind", image.kind); aux.set("source", image.source);
            aux.set("name", text(image.name)); aux.set("width", image.width); aux.set("height", image.height);
            aux.set("rowStride", static_cast<double>(image.row_stride)); aux.set("pixelFormat", image.pixel_format);
            aux.set("alphaMode", image.alpha_mode); aux.set("storageOrigin", image.origin);
            aux.set("data", copied(image.pixels, static_cast<std::size_t>(image.byte_count)));
            images.call<void>("push", aux);
        }
        entry.set("images", images);
        list.call<void>("push", entry);
    }
    return list;
}

val surfaces(const iprop3d_result* result, u32 count, Checked& check) {
    auto list = val::array();
    for (u32 s = 0; s < count; ++s) {
        iprop3d_surface_view view{}; iprop3d_surface_view_init(&view, sizeof(view));
        check(iprop3d_result_surface(result, s, &view, &check.error));
        const std::size_t vertices = view.vertex_count;
        auto entry = val::object();
        entry.set("index", s); entry.set("texture", view.texture_index);
        entry.set("roughness", view.roughness); entry.set("metallic", view.metallic); entry.set("emission", view.emission);
        entry.set("alphaMode", view.alpha_mode); entry.set("alphaCutoff", view.alpha_cutoff);
        entry.set("twoSided", view.two_sided != 0); entry.set("crisp", view.crisp != 0);
        entry.set("vertexCount", view.vertex_count); entry.set("indexCount", view.index_count);
        entry.set("positions", copied(view.positions, vertices * 3));
        entry.set("normals", copied(view.normals, vertices * 3));
        entry.set("tangents", copied(view.tangents, vertices * 4));
        entry.set("uvs", copied(view.uvs, vertices * 2));
        entry.set("indices", copied(view.indices, static_cast<std::size_t>(view.index_count)));
        list.call<void>("push", entry);
    }
    return list;
}

// The model's own outputs (M3): values as a flat Float32Array, three floats
// a point and fifteen a box (centre, size, x, y and z axis).
val outputs(const iprop3d_result* result, u32 count, Checked& check) {
    auto list = val::array();
    for (u32 o = 0; o < count; ++o) {
        iprop3d_output_view view{}; iprop3d_output_view_init(&view, sizeof(view));
        check(iprop3d_result_output(result, o, &view, &check.error));
        auto entry = val::object();
        entry.set("index", o); entry.set("name", text(view.name));
        entry.set("kind", std::string(view.kind == IPROP3D_OUTPUT_BOX3D ? "box3d" : "point3d"));
        entry.set("list", view.list != 0); entry.set("count", view.value_count);
        entry.set("values", copied(view.values, std::size_t{view.value_count} * view.floats_per_value));
        list.call<void>("push", entry);
    }
    return list;
}

val resultObject(const iprop3d_result* result, Checked& check) {
    iprop3d_result_info info{}; iprop3d_result_info_init(&info, sizeof(info));
    check(iprop3d_result_query(result, &info, &check.error));
    auto object = val::object(), bounds = val::object(), fuel = val::object(), work = val::object();
    bounds.set("min", triple(info.bounds_min)); bounds.set("max", triple(info.bounds_max));
    object.set("bounds", bounds); object.set("size", triple(info.size));
    object.set("unitsPerMeter", info.units_per_meter);
    object.set("vertexCount", info.vertex_count); object.set("triangleCount", info.triangle_count);
    fuel.set("budget", static_cast<double>(info.fuel_budget)); fuel.set("used", static_cast<double>(info.fuel_used));
    work.set("budget", static_cast<double>(info.work_budget)); work.set("used", static_cast<double>(info.work_used));
    object.set("fuel", fuel); object.set("work", work);
    object.set("textures", textures(result, info.texture_count, check));
    object.set("surfaces", surfaces(result, info.surface_count, check));
    object.set("outputs", outputs(result, info.output_count, check));
    return object;
}

} // namespace

val modelLibrary() {
    iprop3d_library_info info{}; iprop3d_library_info_init(&info, sizeof(info));
    iprop3d_library_query(&info, nullptr);
    auto object = val::object();
    object.set("apiMajor", info.api_major); object.set("apiMinor", info.api_minor);
    object.set("maxVertices", info.max_vertices); object.set("maxTriangles", info.max_triangles);
    object.set("maxTextures", info.max_textures); object.set("maxTextureSide", info.max_texture_side);
    object.set("maxTexels", static_cast<double>(info.max_texels));
    object.set("maxLod", IPROP3D_MAX_LOD);
    char stamp[17];
    std::snprintf(stamp, sizeof(stamp), "%016llx", static_cast<unsigned long long>(iprop3d_texture_stamp()));
    object.set("textureStamp", std::string(stamp));
    return object;
}

// Loads and validates a model; the description carries its handle.
val modelLoad(val fileBytes) {
    try {
        const auto length = fileBytes["byteLength"].as<std::size_t>();
        std::vector<std::uint8_t> bytes(length ? length : 1);
        val(emscripten::typed_memory_view(length, bytes.data())).call<void>("set", fileBytes);
        Checked check;
        iprop3d_model* raw = nullptr;
        check(iprop3d_model_load(bytes.data(), length, &raw, &check.error));
        Model model(raw, iprop3d_model_free);
        auto object = describe(model.get(), check);
        const u32 handle = g_nextModel++;
        g_models.emplace(handle, std::move(model));
        object.set("handle", handle);
        return object;
    } catch (const ApiError& error) { return errorResult(error.what(), &error.detail); }
      catch (const std::exception& error) { return errorResult(error.what()); }
}

// Releases a model. Returns whether the handle named one.
bool modelFree(u32 handle) { return g_models.erase(handle) != 0; }

u32 modelCount() { return static_cast<u32>(g_models.size()); }

// `known` is an array of { key, fuel, work, kinds }: textures the page holds.
std::vector<iprop3d_known_texture> readKnown(val known) {
    std::vector<iprop3d_known_texture> records;
    if (known.isUndefined() || known.isNull()) return records;
    const u32 count = known["length"].as<u32>();
    records.reserve(count);
    for (u32 i = 0; i < count; ++i) {
        const val entry = known[i];
        const std::string key = entry["key"].as<std::string>();
        iprop3d_known_texture record{};
        if (key.size() != 16) throw std::invalid_argument("a known texture's key must be sixteen hex digits");
        for (const char c : key) {
            std::uint8_t digit = 0;
            if (!hexDigit(c, digit)) throw std::invalid_argument("a known texture's key must be sixteen hex digits");
            record.key = (record.key << 4) | digit;
        }
        const auto counter = [&](const char* name) {
            const double value = entry[name].as<double>();
            if (!(value >= 0) || value > 9007199254740991.0 || std::floor(value) != value)
                throw std::invalid_argument(std::string("a known texture's ") + name + " must be a nonnegative integer");
            return static_cast<std::uint64_t>(value);
        };
        record.fuel = counter("fuel");
        record.work = counter("work");
        record.image_kinds = static_cast<u32>(counter("kinds"));
        records.push_back(record);
    }
    return records;
}

val modelGenerate(u32 handle, u32 seed, val uniforms, val execution, u32 lod, val known) {
    try {
        const auto found = g_models.find(handle);
        if (found == g_models.end()) throw std::invalid_argument("the model is not loaded: it was freed");
        const iprop3d_model* model = found->second.get();
        Checked check;
        iprop_parameters* rawParameters = nullptr;
        check(iprop3d_model_parameters_create(model, &rawParameters, &check.error));
        const std::unique_ptr<iprop_parameters, decltype(&iprop_parameters_free)> parameters(rawParameters, iprop_parameters_free);
        readUniforms(model, parameters.get(), uniforms, check);
        iprop3d_generate_options options{}; iprop3d_generate_options_init(&options, sizeof(options));
        options.seed = seed;
        options.lod = lod;
        const auto records = readKnown(known);
        options.known_textures = records.empty() ? nullptr : records.data();
        options.known_texture_count = static_cast<u32>(records.size());
        iprop_execution_limits_ex limits{}; iprop_execution_limits_ex_init(&limits, sizeof(limits));
        if (!execution.isUndefined() && !execution.isNull()) {
            const auto number = [&](const char* name) {
                const auto value = execution[name];
                if (value.isUndefined()) return 0.0;
                if (!value.isNumber()) throw std::invalid_argument(std::string(name) + " must be a number");
                const double result = value.as<double>();
                if (!std::isfinite(result) || result < 0 || result > 9007199254740991.0)
                    throw std::invalid_argument(std::string(name) + " is outside its supported range");
                return result;
            };
            const auto work = number("workLimit"), timeout = number("timeoutMs") * 1000000.0;
            if (std::floor(work) != work || timeout > 9007199254740991.0)
                throw std::invalid_argument("execution limits must have exact, representable counters");
            limits.work_limit = static_cast<std::uint64_t>(work);
            limits.timeout_ns = static_cast<std::uint64_t>(std::ceil(timeout));
            limits.poll_context = &execution;
            limits.poll = [](void* context) -> u32 { return iprop_cancel_js(static_cast<val*>(context)->as_handle()); };
        }
        iprop3d_result* rawResult = nullptr;
        check(iprop3d_generate(model, &options, parameters.get(), &limits, &rawResult, &check.error));
        const std::unique_ptr<iprop3d_result, decltype(&iprop3d_result_free)> result(rawResult, iprop3d_result_free);
        return resultObject(result.get(), check);
    // Return normally through the WASM/C++ frames, including catch cleanup.
    // module_api3d.js throws the owned JS Error only after this call completes.
    } catch (const ApiError& error) { return errorResult(error.what(), &error.detail); }
      catch (const std::exception& error) { return errorResult(error.what()); }
}

EMSCRIPTEN_BINDINGS(iprop3d) {
    emscripten::function("_modelLibrary", &modelLibrary);
    emscripten::function("_modelLoad", &modelLoad);
    emscripten::function("_modelFree", &modelFree);
    emscripten::function("_modelCount", &modelCount);
    emscripten::function("_modelGenerate", &modelGenerate);
}
