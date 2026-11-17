// Models in the Godot GDExtension (preview), over the model C API
// (backends/capi3d).
//
// IpropModel (Resource): loads an .iprop3d file, keeps one parameter set for
// its inputs and generates one seed into an ArrayMesh with its materials.
// The generation's own numbers stay available as properties: the raw arrays
// of every surface and the images of every texture, exactly as generated.
//
// IpropModelInstance3D (MeshInstance3D): a convenience node holding an
// IpropModel and a seed; refresh() regenerates the mesh.
//
// A model and Godot agree on everything but two conventions, which the mesh
// adapts and the raw arrays do not: Godot's front faces are clockwise, so
// every triangle's index order is reversed, and a scene is in metres, so
// positions are divided by units_per_meter. Axes (+Y up, right-handed), UVs
// (the upper left corner is the origin), tangents with a bitangent sign and
// normal maps with green up the image are the same on both sides.

#pragma once

#include <map>
#include <string>

#include "iprop3d.h"
#include "iprop_godot.hpp"

#include <godot_cpp/classes/array_mesh.hpp>
#include <godot_cpp/classes/image_texture.hpp>
#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/variant.hpp>
#include <godot_cpp/variant/vector3.hpp>

namespace iprop_godot {

// A loaded .iprop3d plus the latest generation.
class IpropModel : public godot::Resource {
    GDCLASS(IpropModel, godot::Resource)

    iprop3d_model* model_ = nullptr;
    iprop_parameters* parameters_ = nullptr; // created with each load
    godot::Vector3 size_{};
    double units_per_meter_ = 0.0;
    int64_t fuel_ = 0;
    godot::Array skin_programs_{};
    // The latest generation; a failed one leaves these as they were.
    godot::Array surfaces_{}, textures_{};
    godot::Vector3 bounds_min_{}, bounds_max_{};
    int64_t vertex_count_ = 0, triangle_count_ = 0, fuel_used_ = 0;
    godot::Dictionary work_{}, last_error_{};
    // GPU textures by texture key and image kind (0 is the colour image):
    // equal keys mean equal pixels, so the meshes of several generations
    // share them. An entry that no mesh holds any more is dropped at the
    // next generate.
    std::map<std::string, godot::Ref<godot::ImageTexture>> texture_cache_;

protected:
    static void _bind_methods();

public:
    // The C tags found in the dictionaries below.
    enum { IMAGE_NORMAL = 1, IMAGE_ROUGHNESS = 2, IMAGE_METALLIC = 3, IMAGE_AO = 4, IMAGE_EMISSION = 5,
           ALPHA_OPAQUE = 0, ALPHA_CUTOUT = 1, ALPHA_BLEND = 2 };

    IpropModel();
    ~IpropModel() override;

    // Loads and validates an .iprop3d (a res:// or absolute path). Returns
    // OK, ERR_FILE_NOT_FOUND or ERR_INVALID_DATA (the error is printed). A
    // successful load starts from the model's default inputs and forgets the
    // previous generation.
    godot::Error load_file(const godot::String& path);
    bool is_loaded() const { return model_ != nullptr; }

    // The inputs in declaration order, set and reset as in IpropAsset:
    // {index, name, type ("color", "f32" or "u32"), default, value}.
    godot::Array get_inputs() const;
    godot::Error set_input(const godot::String& name, const godot::Variant& value);
    godot::Error reset_inputs();

    // Generates one seed and returns its mesh: a surface per material, in
    // metres, with a StandardMaterial3D each. `limits` is as in
    // IpropAsset.generate (work_limit, timeout_ns, cancellation) and covers
    // the model and every texture it generates. A failure returns null, sets
    // last_error and keeps the previous generation's properties.
    godot::Ref<godot::ArrayMesh> generate(int64_t seed, const godot::Dictionary& limits = godot::Dictionary());

    // The nominal size the source declares, in model units, and the units to
    // a metre.
    godot::Vector3 get_size() const { return size_; }
    double get_units_per_meter() const { return units_per_meter_; }
    int64_t get_fuel() const { return fuel_; }
    // The 2D programs the model carries: [{index, name, width, height}].
    godot::Array get_skin_programs() const { return skin_programs_; }

    // The latest generation as generated, in model units and with the
    // model's counterclockwise triangles. A surface is {index, texture,
    // roughness, metallic, emission, alpha_mode, alpha_cutoff, two_sided,
    // crisp, vertex_count, index_count, positions, normals, tangents, uvs
    // (PackedFloat32Array: 3, 3, 4 and 2 floats a vertex), indices
    // (PackedInt32Array)}.
    godot::Array get_surfaces() const { return surfaces_; }
    // A texture is {index, key, name, width, height, asset_width,
    // asset_height, repeats, image, images, normal}: `image` is the RGBA8
    // colour image, `images` its auxiliary images as {index, kind, source,
    // name, image} and `normal` the normal map among them, or null. `key` is
    // sixteen hex digits: equal keys mean equal pixels.
    godot::Array get_textures() const { return textures_; }
    godot::Vector3 get_bounds_min() const { return bounds_min_; }
    godot::Vector3 get_bounds_max() const { return bounds_max_; }
    int64_t get_vertex_count() const { return vertex_count_; }
    int64_t get_triangle_count() const { return triangle_count_; }
    int64_t get_fuel_used() const { return fuel_used_; }
    // {budget, used}: the work of the model and all its textures.
    godot::Dictionary get_work() const { return work_; }
    godot::Dictionary get_last_error() const { return last_error_; }
    // The GPU textures shared between generations, by texture key and image
    // kind. clear_texture_cache stops sharing with the meshes made so far.
    void clear_texture_cache() { texture_cache_.clear(); }
    int64_t get_cached_texture_count() const { return static_cast<int64_t>(texture_cache_.size()); }
};

class IpropModelInstance3D : public godot::MeshInstance3D {
    GDCLASS(IpropModelInstance3D, godot::MeshInstance3D)

    godot::Ref<IpropModel> model_;
    int64_t seed_ = 0;

protected:
    static void _bind_methods();

public:
    IpropModelInstance3D() = default;
    ~IpropModelInstance3D() override = default;

    // Public: the godot-cpp virtual binding inspects the override from the
    // generated Node template, which needs the access.
    void _ready() override;

    // Regenerates the mesh from the current model and seed. A model that is
    // not loaded, or a failed generation, leaves the node without a mesh.
    void refresh();

    void set_model(const godot::Ref<IpropModel>& model);
    godot::Ref<IpropModel> get_model() const { return model_; }
    void set_seed(int64_t seed);
    int64_t get_seed() const { return seed_; }
};

} // namespace iprop_godot
