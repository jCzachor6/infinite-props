// The Godot GDExtension over the C API (tasks 8.1/8.2).
//
// IpropAsset (Resource): loads a .iprop file through the ipropc library,
// keeps one parameter set for its root inputs, generates one (seed) tuple into a Godot Image and exposes the meta and the structured
// outputs as properties.
//
// IpropSprite2D (Sprite2D): a convenience node holding an IpropAsset and a
// seed and inputs; refresh() regenerates the texture. When the generation
// publishes a normal map the texture is a CanvasTexture carrying it, so
// Light2D nodes light the sprite.

#pragma once

#include "iprop.h"

#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/classes/sprite2d.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/packed_float32_array.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/variant.hpp>
#include <godot_cpp/variant/vector2.hpp>
#include <godot_cpp/variant/vector2i.hpp>

namespace iprop_godot {

// Independent one-way atomic cancellation; a Ref held by generate keeps its
// native handle alive while another thread cancels it.
class IpropCancellation : public godot::RefCounted {
    GDCLASS(IpropCancellation, godot::RefCounted)
    iprop_cancellation* token_ = nullptr;
protected:
    static void _bind_methods();
public:
    IpropCancellation();
    ~IpropCancellation() override;
    bool is_valid() const { return token_ != nullptr; }
    void cancel() { iprop_cancellation_cancel(token_); }
    bool is_cancelled() const { return iprop_cancellation_is_cancelled(token_) != 0; }
    const iprop_cancellation* handle() const { return token_; }
};

// A loaded .iprop plus the latest generation's outputs.
class IpropAsset : public godot::Resource {
    GDCLASS(IpropAsset, godot::Resource)

    iprop_program* program_ = nullptr;
    iprop_parameters* parameters_ = nullptr; // created with each load
    godot::Vector2i size_{};
    godot::Vector2 origin_{};
    int64_t fuel_ = 0;
    godot::Dictionary outputs_{};
    godot::Dictionary part_outputs_{};
    godot::Array output_slots_{};
    godot::Array resources_{};
    godot::Dictionary work_{}, last_error_{};
    godot::Array images_{};             // the latest generation's auxiliary images
    godot::Ref<godot::Image> normal_{}; // its normal map, or null
    bool derive_normal_ = false;
    double normal_depth_ = 0.0, normal_rounding_ = 0.0, normal_detail_ = 0.0;

protected:
    static void _bind_methods();

public:
    // The C tags found in image dictionaries.
    enum { IMAGE_NORMAL = 1, SOURCE_AUTHORED = 1, SOURCE_DERIVED = 2 };

    IpropAsset();
    ~IpropAsset() override;

    // Loads and validates a .iprop (a res:// or absolute path). Returns OK,
    // ERR_FILE_NOT_FOUND or ERR_INVALID_DATA (the last error is printed).
    // A successful load starts from the program's default inputs.
    godot::Error load_file(const godot::String& path);
    bool is_loaded() const { return program_ != nullptr; }

    // The root inputs in declaration order: {index, name, type ("color",
    // "f32" or "u32"), default, value}. Colors are Color, f32 float, u32 int.
    godot::Array get_inputs() const;
    // Sets one input for later generations: a Color for a color input, a
    // number for f32, an int in -2^31..2^32-1 for u32 (negative values keep
    // their i32 bits). Returns OK, ERR_UNCONFIGURED before a load, or
    // ERR_INVALID_PARAMETER for an unknown name, a mismatched type or an out
    // of range value (the error and its reason code are printed). Success
    // emits `changed`.
    godot::Error set_input(const godot::String& name, const godot::Variant& value);
    // Restores every input to its default; emits `changed`.
    godot::Error reset_inputs();

    // Generates one tuple; returns the RGBA8 image. The structured outputs
    // are copied and exposed through get_outputs(). A failure preserves the
    // previously completed generation's maps; returned Images own their bytes.
    godot::Ref<godot::Image> generate(int64_t seed, int64_t width = 0, int64_t height = 0,
                                    const godot::Dictionary& limits = godot::Dictionary());

    godot::Vector2i get_size() const { return size_; }
    godot::Vector2 get_origin() const { return origin_; }
    int64_t get_fuel() const { return fuel_; }
    godot::Dictionary get_outputs() const { return outputs_; }
    godot::Dictionary get_part_outputs() const { return part_outputs_; }
    godot::Array get_output_slots() const { return output_slots_; }
    godot::Array get_resources() const { return resources_; }
    godot::Dictionary get_work() const { return work_; }
    godot::Dictionary get_last_error() const { return last_error_; }
    godot::Dictionary get_resource(int64_t id) const {
        return id > 0 && id <= resources_.size() ? godot::Dictionary(resources_[id - 1]) : godot::Dictionary();
    }

    // The auxiliary images the loaded program can publish, [{index, kind,
    // name}]: the normal output of its root asset, or nothing.
    godot::Array get_declared_images() const;
    // The latest generation's auxiliary images in result order: {index, kind,
    // source, name, image}. kind is IMAGE_NORMAL; source is SOURCE_AUTHORED
    // or SOURCE_DERIVED. Each Image owns its bytes.
    godot::Array get_images() const { return images_; }
    // The latest generation's normal map, or null. R is x rightward, G is y
    // upward and B is z toward the viewer, the convention Godot expects; a
    // byte b decodes as b / 127.5 - 1 and alpha is 255 everywhere.
    godot::Ref<godot::Image> get_normal_image() const { return normal_; }
    // derive_normal asks the runtime for a normal map when the program
    // publishes none. Depth is the height of full coverage and rounding a
    // Gaussian sigma, both in image pixels; detail is the share of brightness
    // in the height (0..1). Zero selects each default. Out of range values
    // fail the next generate with an argument error. Changes emit `changed`.
    void set_derive_normal(bool enabled);
    bool get_derive_normal() const { return derive_normal_; }
    void set_normal_depth(double value);
    double get_normal_depth() const { return normal_depth_; }
    void set_normal_rounding(double value);
    double get_normal_rounding() const { return normal_rounding_; }
    void set_normal_detail(double value);
    double get_normal_detail() const { return normal_detail_; }
};

class IpropSprite2D : public godot::Sprite2D {
    GDCLASS(IpropSprite2D, godot::Sprite2D)

    godot::Ref<IpropAsset> asset_;
    int64_t seed_ = 0;

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
};

} // namespace iprop_godot
