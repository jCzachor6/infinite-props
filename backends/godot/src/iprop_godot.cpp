// The GDExtension implementation (see iprop_godot.hpp).

#include "iprop_godot.hpp"
#include "../../ResultQueries.hpp"

#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/image_texture.hpp>
#include <godot_cpp/classes/texture2d.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

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
bool outputName(iprop_utf8_view view, String& name) {
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

} // namespace

// ---- IpropAsset -----------------------------------------------------------

void IpropAsset::_bind_methods() {
    ClassDB::bind_method(D_METHOD("load_file", "path"), &IpropAsset::load_file);
    ClassDB::bind_method(D_METHOD("is_loaded"), &IpropAsset::is_loaded);
    ClassDB::bind_method(D_METHOD("generate", "seed", "variant", "archetype"),
                         &IpropAsset::generate);
    ClassDB::bind_method(D_METHOD("get_size"), &IpropAsset::get_size);
    ClassDB::bind_method(D_METHOD("get_origin"), &IpropAsset::get_origin);
    ClassDB::bind_method(D_METHOD("get_variants"), &IpropAsset::get_variants);
    ClassDB::bind_method(D_METHOD("get_archetypes"), &IpropAsset::get_archetypes);
    ClassDB::bind_method(D_METHOD("get_fuel"), &IpropAsset::get_fuel);
    ClassDB::bind_method(D_METHOD("get_outputs"), &IpropAsset::get_outputs);
    ClassDB::bind_method(D_METHOD("get_part_outputs"), &IpropAsset::get_part_outputs);
    ClassDB::bind_method(D_METHOD("get_output_slots"), &IpropAsset::get_output_slots);

    ADD_PROPERTY(PropertyInfo(Variant::VECTOR2I, "size"), "", "get_size");
    ADD_PROPERTY(PropertyInfo(Variant::VECTOR2, "origin"), "", "get_origin");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "variants"), "", "get_variants");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "archetypes"), "", "get_archetypes");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "fuel"), "", "get_fuel");
    ADD_PROPERTY(PropertyInfo(Variant::DICTIONARY, "outputs"), "",
                 "get_outputs");
    ADD_PROPERTY(PropertyInfo(Variant::DICTIONARY, "part_outputs"), "",
                 "get_part_outputs");
    ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "output_slots"), "",
                 "get_output_slots");
}

IpropAsset::IpropAsset() = default;

IpropAsset::~IpropAsset() {
    iprop_program_free(program_);
    program_ = nullptr;
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
    char error[512] = {};
    iprop_program* candidate = nullptr;
    const iprop_status status = iprop_program_load(
        bytes.ptr(), static_cast<size_t>(bytes.size()), &candidate, error,
        sizeof(error));
    if (status != IPROP_OK) {
        UtilityFunctions::printerr(
            "iprop: load failed: ",
            String::utf8(error[0] != '\0' ? error : "unknown error"));
        return ERR_INVALID_DATA;
    }
    iprop_program_info_ex meta;
    iprop_error_ex query_error;
    const bool initialized =
        iprop_program_info_ex_init(&meta, sizeof(meta)) == IPROP_EX_OK &&
        iprop_error_ex_init(&query_error, sizeof(query_error)) == IPROP_EX_OK;
    if (initialized) {
        query_error.message_buffer = error;
        query_error.message_capacity = sizeof(error);
    }
    if (!initialized || iprop_program_query(candidate, &meta, &query_error) != IPROP_EX_OK) {
        iprop_program_free(candidate);
        UtilityFunctions::printerr(
            "iprop: metadata query failed: ",
            String::utf8(initialized && error[0] != '\0' ? error : "unknown error"));
        return ERR_INVALID_DATA;
    }
    iprop_program_free(program_);
    program_ = candidate;
    outputs_.clear();
    part_outputs_.clear();
    output_slots_.clear();
    size_ = Vector2i(static_cast<int>(meta.size_x),
                     static_cast<int>(meta.size_y));
    origin_ = Vector2(meta.origin_x, meta.origin_y);
    variants_ = static_cast<int64_t>(meta.variants);
    archetypes_ = static_cast<int64_t>(meta.archetypes);
    fuel_ = static_cast<int64_t>(meta.fuel);
    emit_changed();
    return OK;
}

Ref<Image> IpropAsset::generate(int64_t seed, int64_t variant,
                                int64_t archetype) {
    const auto validU32 = [](int64_t value) {
        return value >= 0 && value <= std::numeric_limits<uint32_t>::max();
    };
    if (!validU32(seed) || !validU32(variant) || !validU32(archetype)) {
        UtilityFunctions::printerr("iprop: seed, variant and archetype must fit u32");
        return Ref<Image>();
    }
    if (program_ == nullptr) {
        UtilityFunctions::printerr("iprop: generate before load_file");
        return Ref<Image>();
    }
    iprop_generate_options options{};
    options.seed = static_cast<uint32_t>(seed);
    options.variant = static_cast<uint32_t>(variant);
    options.archetype = static_cast<uint32_t>(archetype);
    char error[512] = {};
    iprop_result* result = nullptr;
    const iprop_status status =
        iprop_generate(program_, &options, &result, error, sizeof(error));
    if (status != IPROP_OK) {
        UtilityFunctions::printerr(
            "iprop: generate failed: ",
            String::utf8(error[0] != '\0' ? error : "unknown error"));
        return Ref<Image>();
    }
    const std::unique_ptr<iprop_result, decltype(&iprop_result_free)> owned(result, iprop_result_free);
    iprop_error_ex query_error;
    iprop_error_ex_init(&query_error, sizeof(query_error));
    query_error.message_buffer = error;
    query_error.message_capacity = sizeof(error);
    const auto readFailure = [&]() {
        UtilityFunctions::printerr("iprop: result read failed: ",
            String::utf8(error[0] != '\0' ? error : "unsupported result or allocation failure"));
        return Ref<Image>();
    };
    iprop_image_view_ex image;
    if (iprop::backend::queryImage(result, image, &query_error) != IPROP_EX_OK ||
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
    if (iprop_result_output_count(result, &output_count, &query_error) != IPROP_EX_OK) return readFailure();
    for (uint32_t i = 0; i < output_count; ++i) {
        iprop_result_output_view_ex slot;
        if (iprop::backend::queryOutput(result, i, slot, &query_error) != IPROP_EX_OK) return readFailure();
        String name;
        if (!outputName(slot.name, name)) {
            return readFailure();
        }
        Dictionary entry;
        entry["kind"] = static_cast<int64_t>(slot.kind);
        entry["list"] = slot.is_list != 0;
        const auto components = iprop::backend::outputComponents(slot.kind);
        if (components == 0) return readFailure();
        PackedFloat32Array values;
        if (values.resize(static_cast<int64_t>(slot.element_count) * components) != OK ||
            iprop::backend::copyOutputValues(result, slot, values.ptrw(), &query_error) != IPROP_EX_OK) {
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
    ClassDB::bind_method(D_METHOD("set_variant", "variant"),
                         &IpropSprite2D::set_variant);
    ClassDB::bind_method(D_METHOD("get_variant"), &IpropSprite2D::get_variant);
    ClassDB::bind_method(D_METHOD("set_archetype", "archetype"),
                         &IpropSprite2D::set_archetype);
    ClassDB::bind_method(D_METHOD("get_archetype"),
                         &IpropSprite2D::get_archetype);

    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "asset",
                              PROPERTY_HINT_RESOURCE_TYPE, "IpropAsset"),
                 "set_asset", "get_asset");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "seed"), "set_seed", "get_seed");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "variant"), "set_variant",
                 "get_variant");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "archetype"), "set_archetype",
                 "get_archetype");
}

void IpropSprite2D::_ready() {
    refresh();
}

void IpropSprite2D::refresh() {
    if (asset_.is_null() || !asset_->is_loaded()) {
        set_texture(Ref<Texture2D>());
        return;
    }
    const Ref<Image> image = asset_->generate(seed_, variant_, archetype_);
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

void IpropSprite2D::set_variant(int64_t variant) {
    variant_ = variant;
    refresh();
}

void IpropSprite2D::set_archetype(int64_t archetype) {
    archetype_ = archetype;
    refresh();
}

} // namespace iprop_godot
