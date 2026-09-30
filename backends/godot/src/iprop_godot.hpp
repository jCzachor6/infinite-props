// The Godot GDExtension over the C API (tasks 8.1/8.2).
//
// IpropAsset (Resource): loads a .iprop file through the ipropc library,
// generates one (seed, variant, archetype) tuple into a Godot Image and
// exposes the meta and the structured outputs as properties.
//
// IpropSprite2D (Sprite2D): a convenience node holding an IpropAsset and a
// seed/variant/archetype; refresh() regenerates the texture.

#pragma once

#include "iprop.h"

#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/classes/sprite2d.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/packed_float32_array.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/vector2.hpp>
#include <godot_cpp/variant/vector2i.hpp>

namespace iprop_godot {

// A loaded .iprop plus the latest generation's outputs.
class IpropAsset : public godot::Resource {
    GDCLASS(IpropAsset, godot::Resource)

    iprop_program* program_ = nullptr;
    godot::Vector2i size_{};
    godot::Vector2 origin_{};
    int64_t variants_ = 0;
    int64_t archetypes_ = 0;
    int64_t fuel_ = 0;
    godot::Dictionary outputs_{};

protected:
    static void _bind_methods();

public:
    IpropAsset();
    ~IpropAsset() override;

    // Loads and validates a .iprop (a res:// or absolute path). Returns OK,
    // ERR_FILE_NOT_FOUND or ERR_INVALID_DATA (the last error is printed).
    godot::Error load_file(const godot::String& path);
    bool is_loaded() const { return program_ != nullptr; }

    // Generates one tuple; returns the RGBA8 image. The structured outputs
    // are stored and exposed through get_outputs().
    godot::Ref<godot::Image> generate(int64_t seed, int64_t variant,
                                      int64_t archetype);

    godot::Vector2i get_size() const { return size_; }
    godot::Vector2 get_origin() const { return origin_; }
    int64_t get_variants() const { return variants_; }
    int64_t get_archetypes() const { return archetypes_; }
    int64_t get_fuel() const { return fuel_; }
    godot::Dictionary get_outputs() const { return outputs_; }
};

class IpropSprite2D : public godot::Sprite2D {
    GDCLASS(IpropSprite2D, godot::Sprite2D)

    godot::Ref<IpropAsset> asset_;
    int64_t seed_ = 0;
    int64_t variant_ = 0;
    int64_t archetype_ = 0;

protected:
    static void _bind_methods();

public:
    IpropSprite2D() = default;
    ~IpropSprite2D() override = default;

    // Public: the godot-cpp virtual binding inspects the override from the
    // generated Node template, which needs the access.
    void _ready() override;

    // Regenerates the texture from the current asset and indices.
    void refresh();

    void set_asset(const godot::Ref<IpropAsset>& asset);
    godot::Ref<IpropAsset> get_asset() const { return asset_; }
    void set_seed(int64_t seed);
    int64_t get_seed() const { return seed_; }
    void set_variant(int64_t variant);
    int64_t get_variant() const { return variant_; }
    void set_archetype(int64_t archetype);
    int64_t get_archetype() const { return archetype_; }
};

} // namespace iprop_godot
