// The GDExtension implementation (see iprop_godot.hpp).

#include "iprop_godot.hpp"

#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/image_texture.hpp>
#include <godot_cpp/classes/texture2d.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <cstring>
#include <limits>
#include <vector>

using namespace godot;

namespace iprop_godot {

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
    iprop_meta meta;
    iprop_program_meta(candidate, &meta);
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
    outputs_.clear();
    part_outputs_.clear();
    output_slots_.clear();
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
    iprop_image image{};
    iprop_result_image(result, &image);

    PackedByteArray bytes;
    bytes.resize(static_cast<int64_t>(image.width) * image.height * 4);
    std::memcpy(bytes.ptrw(), image.pixels,
                static_cast<size_t>(image.width) * image.height * 4);

    // The structured outputs: name -> { kind, list, values }.
    for (uint32_t i = 0; i < image.output_count; ++i) {
        const iprop_output& slot = image.outputs[i];
        Dictionary entry;
        entry["kind"] = static_cast<int64_t>(slot.kind);
        entry["list"] = slot.is_list != 0;
        const int64_t floatsPerValue =
            slot.kind == IPROP_OUTPUT_POINT ? 2
            : slot.kind == IPROP_OUTPUT_RECT ? 4
                                             : 3;
        PackedFloat32Array values;
        values.resize(slot.count * floatsPerValue);
        if (slot.count != 0) {
            std::memcpy(values.ptrw(), slot.values,
                        static_cast<size_t>(slot.count) * floatsPerValue *
                            sizeof(float));
        }
        entry["values"] = values;
        entry["name"] = String::utf8(slot.name);
        entry["owner"] = static_cast<int64_t>(slot.owner);
        output_slots_.append(entry);
        if (slot.owner == 0) {
            outputs_[String::utf8(slot.name)] = entry;
        } else {
            const int64_t owner = slot.owner;
            Dictionary part = part_outputs_.get(owner, Dictionary());
            part[String::utf8(slot.name)] = entry;
            part_outputs_[owner] = part;
        }
    }

    iprop_result_free(result);
    return Image::create_from_data(static_cast<int>(image.width),
                                   static_cast<int>(image.height), false,
                                   Image::FORMAT_RGBA8, bytes);
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
