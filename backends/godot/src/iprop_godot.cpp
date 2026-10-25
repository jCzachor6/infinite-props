// The GDExtension implementation (see iprop_godot.hpp).

#include "iprop_godot.hpp"
#include "../../ResultQueries.hpp"

#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/image_texture.hpp>
#include <godot_cpp/classes/texture2d.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <vector>

using namespace godot;

namespace iprop_godot {

namespace {

// Godot's UTF-8 parser stops at embedded NUL even with an explicit length.
// Decode bounded non-NUL segments with its parser, then store each U+0000
// directly in the length-counted String. No UTF-8 decoder is duplicated here.
bool decodeName(iprop_utf8_view view, String& name) {
    if (view.byte_count == 0) return true;
    if (std::memchr(view.data, 0, view.byte_count) == nullptr) {
        return name.parse_utf8(view.data, view.byte_count) == OK;
    }
    if (name.resize(static_cast<int64_t>(view.byte_count) + 1) != OK) return false;
    char32_t* target = name.ptrw();
    int64_t used = 0;
    uint32_t begin = 0;
    while (begin < view.byte_count) {
        const char* zero = static_cast<const char*>(std::memchr(view.data + begin, 0, view.byte_count - begin));
        const auto end = zero == nullptr ? view.byte_count : static_cast<uint32_t>(zero - view.data);
        if (end != begin) {
            String segment;
            if (segment.parse_utf8(view.data + begin, end - begin) != OK) return false;
            std::memcpy(target + used, segment.ptr(), static_cast<size_t>(segment.length()) * sizeof(char32_t));
            used += segment.length();
        }
        if (zero == nullptr) break;
        target[used++] = 0;
        begin = end + 1;
    }
    target[used] = 0; // String::resize requires the caller to terminate storage.
    return name.resize(used + 1) == OK;
}

// A structured C API error with its own message buffer.
class Failure {
public:
    Failure() {
        iprop_error_ex_init(&error_, sizeof(error_));
        error_.message_buffer = message_;
        error_.message_capacity = sizeof(message_);
    }
    Failure(const Failure&) = delete;
    Failure& operator=(const Failure&) = delete;

    iprop_error_ex* get() { return &error_; }

    // Prints "iprop: <operation> failed: <message> (reason N)".
    void print(const char* operation, const char* fallback = "unknown error") const {
        UtilityFunctions::printerr("iprop: ", operation, " failed: ",
            String::utf8(message_[0] != '\0' ? message_ : fallback),
            " (reason ", static_cast<int64_t>(error_.reason), ")");
    }

private:
    iprop_error_ex error_;
    char message_[512] = {};
};

const char* typeName(iprop_input_type type) {
    switch (type) {
    case IPROP_INPUT_COLOR: return "color";
    case IPROP_INPUT_F32: return "f32";
    default: return "u32";
    }
}

Variant inputValue(iprop_input_type type, iprop_color_rgba8 color, float f32, uint32_t u32) {
    switch (type) {
    case IPROP_INPUT_COLOR: return Color::from_rgba8(color.r, color.g, color.b, color.a);
    case IPROP_INPUT_F32: return f32;
    default: return static_cast<int64_t>(u32);
    }
}

} // namespace

// ---- IpropAsset -----------------------------------------------------------

void IpropAsset::_bind_methods() {
    ClassDB::bind_method(D_METHOD("load_file", "path"), &IpropAsset::load_file);
    ClassDB::bind_method(D_METHOD("is_loaded"), &IpropAsset::is_loaded);
    ClassDB::bind_method(D_METHOD("get_inputs"), &IpropAsset::get_inputs);
    ClassDB::bind_method(D_METHOD("set_input", "name", "value"), &IpropAsset::set_input);
    ClassDB::bind_method(D_METHOD("reset_inputs"), &IpropAsset::reset_inputs);
    ClassDB::bind_method(D_METHOD("generate", "seed", "width", "height"),
                         &IpropAsset::generate, DEFVAL(0), DEFVAL(0));
    ClassDB::bind_method(D_METHOD("get_size"), &IpropAsset::get_size);
    ClassDB::bind_method(D_METHOD("get_origin"), &IpropAsset::get_origin);
    ClassDB::bind_method(D_METHOD("get_fuel"), &IpropAsset::get_fuel);
    ClassDB::bind_method(D_METHOD("get_outputs"), &IpropAsset::get_outputs);
    ClassDB::bind_method(D_METHOD("get_part_outputs"), &IpropAsset::get_part_outputs);
    ClassDB::bind_method(D_METHOD("get_output_slots"), &IpropAsset::get_output_slots);

    ADD_PROPERTY(PropertyInfo(Variant::VECTOR2I, "size"), "", "get_size");
    ADD_PROPERTY(PropertyInfo(Variant::VECTOR2, "origin"), "", "get_origin");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "fuel"), "", "get_fuel");
    ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "inputs"), "", "get_inputs");
    ADD_PROPERTY(PropertyInfo(Variant::DICTIONARY, "outputs"), "",
                 "get_outputs");
    ADD_PROPERTY(PropertyInfo(Variant::DICTIONARY, "part_outputs"), "",
                 "get_part_outputs");
    ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "output_slots"), "",
                 "get_output_slots");
}

IpropAsset::IpropAsset() = default;

IpropAsset::~IpropAsset() {
    iprop_parameters_free(parameters_);
    iprop_program_free(program_);
}

Error IpropAsset::load_file(const String& path) {
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
    iprop_program* loaded = nullptr;
    if (iprop_program_load_ex(bytes.ptr(), static_cast<uint64_t>(bytes.size()), &loaded,
                              failure.get()) != IPROP_EX_OK) {
        failure.print("load");
        return ERR_INVALID_DATA;
    }
    std::unique_ptr<iprop_program, decltype(&iprop_program_free)> candidate(loaded, iprop_program_free);
    iprop_program_info_ex meta;
    if (iprop_program_info_ex_init(&meta, sizeof(meta)) != IPROP_EX_OK ||
        iprop_program_query(candidate.get(), &meta, failure.get()) != IPROP_EX_OK) {
        failure.print("metadata query");
        return ERR_INVALID_DATA;
    }
    iprop_parameters* parameters = nullptr;
    if (iprop_parameters_create(candidate.get(), &parameters, failure.get()) != IPROP_EX_OK) {
        failure.print("parameter setup");
        return ERR_INVALID_DATA;
    }
    iprop_parameters_free(parameters_);
    iprop_program_free(program_);
    parameters_ = parameters;
    program_ = candidate.release();
    outputs_.clear();
    part_outputs_.clear();
    output_slots_.clear();
    size_ = Vector2i(static_cast<int>(meta.size_x),
                     static_cast<int>(meta.size_y));
    origin_ = Vector2(meta.origin_x, meta.origin_y);
    fuel_ = static_cast<int64_t>(meta.fuel);
    emit_changed();
    return OK;
}

Array IpropAsset::get_inputs() const {
    Array inputs;
    if (program_ == nullptr) return inputs;
    Failure failure;
    uint32_t count = 0;
    if (iprop_program_input_count(program_, &count, failure.get()) != IPROP_EX_OK) {
        failure.print("input query");
        return inputs;
    }
    for (uint32_t i = 0; i < count; ++i) {
        iprop_input_info_ex info;
        iprop_parameter_value_ex value;
        String name;
        if (iprop_input_info_ex_init(&info, sizeof(info)) != IPROP_EX_OK ||
            iprop_parameter_value_ex_init(&value, sizeof(value)) != IPROP_EX_OK ||
            iprop_program_input_get(program_, i, &info, failure.get()) != IPROP_EX_OK ||
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

Error IpropAsset::set_input(const String& name, const Variant& value) {
    if (parameters_ == nullptr) {
        UtilityFunctions::printerr("iprop: set_input before load_file");
        return ERR_UNCONFIGURED;
    }
    const CharString bytes = name.utf8();
    const iprop_utf8_view view{bytes.get_data(), static_cast<uint32_t>(bytes.length())};
    Failure failure;
    iprop_parameter_value_ex current;
    if (iprop_parameter_value_ex_init(&current, sizeof(current)) != IPROP_EX_OK ||
        iprop_parameters_get_named(parameters_, view, &current, failure.get()) != IPROP_EX_OK) {
        failure.print("set_input");
        return ERR_INVALID_PARAMETER;
    }
    // The declared type picks the setter; u32 inputs also take negative i32 bits.
    const Variant::Type kind = value.get_type();
    iprop_status_ex status;
    if (current.type == IPROP_INPUT_COLOR && kind == Variant::COLOR) {
        const Color color = value;
        const iprop_color_rgba8 rgba{static_cast<uint8_t>(color.get_r8()), static_cast<uint8_t>(color.get_g8()),
                                     static_cast<uint8_t>(color.get_b8()), static_cast<uint8_t>(color.get_a8())};
        status = iprop_parameters_set_color_named(parameters_, view, rgba, failure.get());
    } else if (current.type == IPROP_INPUT_F32 && (kind == Variant::FLOAT || kind == Variant::INT)) {
        status = iprop_parameters_set_f32_named(parameters_, view, static_cast<float>(static_cast<double>(value)),
                                                failure.get());
    } else if (const int64_t number = kind == Variant::INT ? static_cast<int64_t>(value) : -1;
               current.type == IPROP_INPUT_U32 && kind == Variant::INT &&
               number >= std::numeric_limits<int32_t>::min() && number <= std::numeric_limits<uint32_t>::max()) {
        status = number < 0
            ? iprop_parameters_set_i32_named(parameters_, view, static_cast<int32_t>(number), failure.get())
            : iprop_parameters_set_u32_named(parameters_, view, static_cast<uint32_t>(number), failure.get());
    } else {
        const char* expected = current.type == IPROP_INPUT_COLOR ? "a Color"
                             : current.type == IPROP_INPUT_F32   ? "a number"
                                                                 : "an int in -2147483648..4294967295";
        UtilityFunctions::printerr("iprop: set_input failed: ", name, " takes ", expected);
        return ERR_INVALID_PARAMETER;
    }
    if (status != IPROP_EX_OK) {
        failure.print("set_input");
        return ERR_INVALID_PARAMETER;
    }
    emit_changed();
    return OK;
}

Error IpropAsset::reset_inputs() {
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

Ref<Image> IpropAsset::generate(int64_t seed, int64_t width, int64_t height) {
    if (width < 0 || height < 0 || width > UINT32_MAX || height > UINT32_MAX) {
        UtilityFunctions::printerr("iprop: width/height must be in 0..4294967295");
        return {};
    }
    const auto validU32 = [](int64_t value) {
        return value >= 0 && value <= std::numeric_limits<uint32_t>::max();
    };
    if (!validU32(seed)) {
        UtilityFunctions::printerr("iprop: seed must fit u32");
        return Ref<Image>();
    }
    if (program_ == nullptr) {
        UtilityFunctions::printerr("iprop: generate before load_file");
        return Ref<Image>();
    }
    iprop_generate_options_ex options;
    iprop_generate_options_ex_init(&options, sizeof(options));
    options.seed = static_cast<uint32_t>(seed);
    options.size_x = static_cast<uint32_t>(width);
    options.size_y = static_cast<uint32_t>(height);
    Failure failure;
    iprop_result* result = nullptr;
    if (iprop_generate_ex(program_, &options, parameters_, &result, failure.get()) != IPROP_EX_OK) {
        failure.print("generate");
        return Ref<Image>();
    }
    const std::unique_ptr<iprop_result, decltype(&iprop_result_free)> owned(result, iprop_result_free);
    const auto readFailure = [&]() {
        failure.print("result read", "unsupported result or allocation failure");
        return Ref<Image>();
    };
    iprop_image_view_ex image;
    if (iprop::backend::queryImage(result, image, failure.get()) != IPROP_EX_OK ||
        !iprop::backend::rgba8Image(image) ||
        image.width > static_cast<uint32_t>(std::numeric_limits<int>::max()) ||
        image.height > static_cast<uint32_t>(std::numeric_limits<int>::max()) ||
        image.byte_count > static_cast<uint64_t>(std::numeric_limits<int64_t>::max()) ||
        image.byte_count > std::numeric_limits<size_t>::max()) {
        return readFailure();
    }

    PackedByteArray bytes;
    if (bytes.resize(static_cast<int64_t>(image.byte_count)) != OK) return readFailure();
    if (image.byte_count != 0) {
        std::memcpy(bytes.ptrw(), image.pixels, static_cast<size_t>(image.byte_count));
    }
    const Ref<Image> candidate_image = Image::create_from_data(static_cast<int>(image.width),
        static_cast<int>(image.height), false, Image::FORMAT_RGBA8, bytes);
    if (candidate_image.is_null() || candidate_image->is_empty()) return readFailure();

    Dictionary candidate_outputs, candidate_parts;
    Array candidate_slots;
    uint32_t output_count;
    if (iprop_result_output_count(result, &output_count, failure.get()) != IPROP_EX_OK) return readFailure();
    for (uint32_t i = 0; i < output_count; ++i) {
        iprop_result_output_view_ex slot;
        if (iprop::backend::queryOutput(result, i, slot, failure.get()) != IPROP_EX_OK) return readFailure();
        String name;
        if (!decodeName(slot.name, name)) {
            return readFailure();
        }
        Dictionary entry;
        entry["kind"] = static_cast<int64_t>(slot.kind);
        entry["list"] = slot.is_list != 0;
        const auto components = iprop::backend::outputComponents(slot.kind);
        if (components == 0) return readFailure();
        PackedFloat32Array values;
        if (values.resize(static_cast<int64_t>(slot.element_count) * components) != OK ||
            iprop::backend::copyOutputValues(result, slot, values.ptrw(), failure.get()) != IPROP_EX_OK) {
            return readFailure();
        }
        entry["values"] = values;
        entry["name"] = name;
        entry["index"] = static_cast<int64_t>(slot.output_index);
        entry["owner"] = static_cast<int64_t>(slot.owner);
        candidate_slots.append(entry);
        if (slot.owner == 0) {
            candidate_outputs[name] = entry;
        } else {
            const int64_t owner = slot.owner;
            Dictionary part = candidate_parts.get(owner, Dictionary());
            part[name] = entry;
            candidate_parts[owner] = part;
        }
    }
    outputs_ = candidate_outputs;
    part_outputs_ = candidate_parts;
    output_slots_ = candidate_slots;
    return candidate_image;
}

// ---- IpropSprite2D --------------------------------------------------------

void IpropSprite2D::_bind_methods() {
    ClassDB::bind_method(D_METHOD("refresh"), &IpropSprite2D::refresh);
    ClassDB::bind_method(D_METHOD("set_asset", "asset"),
                         &IpropSprite2D::set_asset);
    ClassDB::bind_method(D_METHOD("get_asset"), &IpropSprite2D::get_asset);
    ClassDB::bind_method(D_METHOD("set_seed", "seed"),
                         &IpropSprite2D::set_seed);
    ClassDB::bind_method(D_METHOD("get_seed"), &IpropSprite2D::get_seed);

    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "asset",
                              PROPERTY_HINT_RESOURCE_TYPE, "IpropAsset"),
                 "set_asset", "get_asset");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "seed"), "set_seed", "get_seed");
}

void IpropSprite2D::_ready() {
    refresh();
}

void IpropSprite2D::refresh() {
    if (asset_.is_null() || !asset_->is_loaded()) {
        set_texture(Ref<Texture2D>());
        return;
    }
    const Ref<Image> image = asset_->generate(seed_);
    if (image.is_null()) {
        set_texture(Ref<Texture2D>());
        return;
    }
    set_texture(ImageTexture::create_from_image(image));
}

void IpropSprite2D::set_asset(const Ref<IpropAsset>& asset) {
    const Callable changed(this, "refresh");
    if (asset_.is_valid() && asset_->is_connected("changed", changed)) {
        asset_->disconnect("changed", changed);
    }
    asset_ = asset;
    if (asset_.is_valid()) {
        asset_->connect("changed", changed);
    }
    refresh();
}

void IpropSprite2D::set_seed(int64_t seed) {
    seed_ = seed;
    refresh();
}



} // namespace iprop_godot
