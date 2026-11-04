// Public WASM generation uses the same checked C API as the native hosts.
// The low-level RGBA view expires on the next call; all typed resources are
// owned JS copies. js/api.js also copies RGBA for independently owned results.
#include <emscripten/bind.h>
#include <emscripten/val.h>
#include "part_values.hpp"

#include <bit>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

using emscripten::val;
using u32 = std::uint32_t;
namespace {
std::vector<std::uint8_t> g_rgba;

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
bool hexDigit(char c, std::uint8_t& out) {
    if (c >= '0' && c <= '9') { out = static_cast<std::uint8_t>(c - '0'); return true; }
    if (c >= 'a' && c <= 'f') { out = static_cast<std::uint8_t>(c - 'a' + 10); return true; }
    if (c >= 'A' && c <= 'F') { out = static_cast<std::uint8_t>(c - 'A' + 10); return true; }
    return false;
}
bool parseColor(const std::string& text, iprop_color_rgba8& color) {
    if ((text.size() != 7 && text.size() != 9) || text[0] != '#') return false;
    std::uint8_t* channels[4] = {&color.r, &color.g, &color.b, &color.a}; color.a = 255;
    for (std::size_t i = 0; i < (text.size() - 1) / 2; ++i) {
        std::uint8_t hi = 0, lo = 0;
        if (!hexDigit(text[1 + i * 2], hi) || !hexDigit(text[2 + i * 2], lo)) return false;
        *channels[i] = static_cast<std::uint8_t>(hi * 16u + lo);
    }
    return true;
}
void readUniforms(const iprop_program* program, iprop_parameters* parameters, val uniforms, Checked& check) {
    if (uniforms.isUndefined() || uniforms.isNull()) return;
    const auto names = emscripten::vecFromJSArray<std::string>(val::global("Object").call<val>("keys", uniforms));
    for (const auto& name : names) {
        u32 index = 0;
        const iprop_utf8_view key{name.data(), static_cast<u32>(name.size())};
        if (iprop_program_input_find(program, key, &index, &check.error) != IPROP_EX_OK)
            throw std::invalid_argument("unknown uniform '" + name + "'");
        iprop_input_info_ex input{}; iprop_input_info_ex_init(&input, sizeof(input));
        check(iprop_program_input_get(program, index, &input, &check.error));
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
val outputs(const iprop_result* result, Checked& check) {
    u32 count = 0; check(iprop_result_output_count(result, &count, &check.error));
    auto list = val::array();
    for (u32 i = 0; i < count; ++i) {
        iprop_output_slot_ex slot{}; check(iprop::backend::querySlot(result, i, slot, &check.error));
        auto entry = val::object(), points = val::array(), rects = val::array(), circles = val::array();
        auto valueBits = val::array(), parts = val::array();
        entry.set("index", slot.output_index); entry.set("owner", slot.owner);
        entry.set("name", slot.name.byte_count ? std::string(slot.name.data, slot.name.byte_count) : std::string());
        entry.set("isList", slot.is_list != 0); entry.set("kind", slot.kind); entry.set("count", slot.element_count);
        if (slot.kind == IPROP_OUTPUT_KIND_PART) {
            std::vector<iprop_part_snapshot_ex> records;
            check(iprop::backend::copyParts(result, slot, records, &check.error));
            for (const auto& p : records) parts.call<void>("push", iprop::html5::part(p));
        } else {
            iprop_result_output_view_ex old{}; check(iprop::backend::queryOutput(result, i, old, &check.error));
            const auto stride = iprop::backend::outputComponents(slot.kind);
            const char* names[] = {"x", "y", slot.kind == IPROP_OUTPUT_KIND_RECT ? "w" : "r", "h"};
            for (u32 k = 0; k < slot.element_count; ++k) {
                auto value = val::object(), bits = val::object();
                for (u32 c = 0; c < stride; ++c) {
                    const float f = old.values[static_cast<std::size_t>(k) * stride + c];
                    bits.set(names[c], std::bit_cast<u32>(f)); value.set(names[c], f);
                }
                valueBits.call<void>("push", bits);
                if (slot.kind == IPROP_OUTPUT_KIND_POINT) points.call<void>("push", value);
                else if (slot.kind == IPROP_OUTPUT_KIND_RECT) rects.call<void>("push", value);
                else circles.call<void>("push", value);
            }
        }
        entry.set("valueBits", valueBits); entry.set("points", points); entry.set("rects", rects);
        entry.set("circles", circles); entry.set("parts", parts); list.call<void>("push", entry);
    }
    return list;
}
} // namespace

val generateWith(val fileBytes, u32 seed, u32 sizeX, u32 sizeY, val uniforms) {
    try {
        const auto length = fileBytes["byteLength"].as<std::size_t>();
        std::vector<std::uint8_t> bytes(length ? length : 1);
        val(emscripten::typed_memory_view(length, bytes.data())).call<void>("set", fileBytes);
        Checked check;
        iprop_program* raw = nullptr; check(iprop_program_load_ex(bytes.data(), length, &raw, &check.error));
        const std::unique_ptr<iprop_program, decltype(&iprop_program_free)> program(raw, iprop_program_free);
        iprop_parameters* rawParameters = nullptr;
        check(iprop_parameters_create(program.get(), &rawParameters, &check.error));
        const std::unique_ptr<iprop_parameters, decltype(&iprop_parameters_free)> parameters(rawParameters, iprop_parameters_free);
        readUniforms(program.get(), parameters.get(), uniforms, check);
        iprop_generate_options_ex options{}; iprop_generate_options_ex_init(&options, sizeof(options));
        options.flags = IPROP_GENERATE_PART_OUTPUTS; options.seed = seed; options.size_x = sizeX; options.size_y = sizeY;
        iprop_result* rawResult = nullptr;
        check(iprop_generate_ex(program.get(), &options, parameters.get(), &rawResult, &check.error));
        const std::unique_ptr<iprop_result, decltype(&iprop_result_free)> result(rawResult, iprop_result_free);
        iprop_image_view_ex image{}; check(iprop::backend::queryImage(result.get(), image, &check.error));
        g_rgba.assign(image.pixels, image.pixels + image.byte_count);
        std::vector<iprop::backend::ResultResource> stores;
        check(iprop::backend::copyResources(result.get(), stores, &check.error));
        auto object = val::object();
        object.set("width", image.width); object.set("height", image.height);
        object.set("rowStride", static_cast<double>(image.row_stride)); object.set("pixelFormat", image.pixel_format);
        object.set("alphaMode", image.alpha_mode); object.set("storageOrigin", image.origin);
        object.set("outputs", outputs(result.get(), check)); object.set("resources", iprop::html5::resources(stores));
        iprop_result_stats_ex stats{}; iprop_result_stats_ex_init(&stats, sizeof(stats));
        check(iprop_result_query_stats(result.get(), &stats, &check.error));
        auto fuel = val::object();
        fuel.set("budget", static_cast<double>(stats.fuel_budget)); fuel.set("remaining", static_cast<double>(stats.fuel_remaining));
        fuel.set("used", static_cast<double>(stats.fuel_used)); object.set("fuel", fuel);
        object.set("data", val(emscripten::typed_memory_view(g_rgba.size(), g_rgba.data())));
        return object;
    // Return normally through the WASM/C++ frames, including catch cleanup.
    // module_api.js throws the owned JS Error only after this call completes.
    } catch (const ApiError& error) { return errorResult(error.what(), &error.detail); }
      catch (const std::exception& error) { return errorResult(error.what()); }
}
val generate(val fileBytes, u32 seed, u32 sizeX, u32 sizeY) {
    return generateWith(fileBytes, seed, sizeX, sizeY, val::undefined());
}
EMSCRIPTEN_BINDINGS(iprop) {
    emscripten::function("_generate", &generate);
    emscripten::function("_generate", &generateWith);
}
