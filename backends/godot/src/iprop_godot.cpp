// The GDExtension implementation (see iprop_godot.hpp).

#include "iprop_godot.hpp"
#include "godot_support.hpp"
#include "part_values.hpp"
#include "../../ResultQueries.hpp"

#include <godot_cpp/classes/canvas_texture.hpp>
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

void IpropCancellation::_bind_methods() {
    ClassDB::bind_method(D_METHOD("is_valid"), &IpropCancellation::is_valid);
    ClassDB::bind_method(D_METHOD("cancel"), &IpropCancellation::cancel);
    ClassDB::bind_method(D_METHOD("is_cancelled"), &IpropCancellation::is_cancelled);
}
IpropCancellation::IpropCancellation() {
    Failure failure;
    if (iprop_cancellation_create(&token_, failure.get()) != IPROP_EX_OK)
        failure.print("cancellation setup");
}
IpropCancellation::~IpropCancellation() { iprop_cancellation_free(token_); }

// ---- IpropAsset -----------------------------------------------------------

void IpropAsset::_bind_methods() {
    ClassDB::bind_method(D_METHOD("load_file", "path"), &IpropAsset::load_file);
    ClassDB::bind_method(D_METHOD("is_loaded"), &IpropAsset::is_loaded);
    ClassDB::bind_method(D_METHOD("get_inputs"), &IpropAsset::get_inputs);
    ClassDB::bind_method(D_METHOD("set_input", "name", "value"), &IpropAsset::set_input);
    ClassDB::bind_method(D_METHOD("reset_inputs"), &IpropAsset::reset_inputs);
    ClassDB::bind_method(D_METHOD("generate", "seed", "width", "height", "limits"),
                         &IpropAsset::generate, DEFVAL(0), DEFVAL(0), DEFVAL(Dictionary()));
    ClassDB::bind_method(D_METHOD("get_size"), &IpropAsset::get_size);
    ClassDB::bind_method(D_METHOD("get_origin"), &IpropAsset::get_origin);
    ClassDB::bind_method(D_METHOD("get_fuel"), &IpropAsset::get_fuel);
    ClassDB::bind_method(D_METHOD("get_outputs"), &IpropAsset::get_outputs);
    ClassDB::bind_method(D_METHOD("get_part_outputs"), &IpropAsset::get_part_outputs);
    ClassDB::bind_method(D_METHOD("get_output_slots"), &IpropAsset::get_output_slots);
    ClassDB::bind_method(D_METHOD("get_resources"), &IpropAsset::get_resources);
    ClassDB::bind_method(D_METHOD("get_work"), &IpropAsset::get_work);
    ClassDB::bind_method(D_METHOD("get_last_error"), &IpropAsset::get_last_error);
    ClassDB::bind_method(D_METHOD("get_resource", "id"), &IpropAsset::get_resource);
    ClassDB::bind_method(D_METHOD("get_declared_images"), &IpropAsset::get_declared_images);
    ClassDB::bind_method(D_METHOD("get_images"), &IpropAsset::get_images);
    ClassDB::bind_method(D_METHOD("get_normal_image"), &IpropAsset::get_normal_image);
    ClassDB::bind_method(D_METHOD("set_derive_normal", "enabled"), &IpropAsset::set_derive_normal);
    ClassDB::bind_method(D_METHOD("get_derive_normal"), &IpropAsset::get_derive_normal);
    ClassDB::bind_method(D_METHOD("set_normal_depth", "value"), &IpropAsset::set_normal_depth);
    ClassDB::bind_method(D_METHOD("get_normal_depth"), &IpropAsset::get_normal_depth);
    ClassDB::bind_method(D_METHOD("set_normal_rounding", "value"), &IpropAsset::set_normal_rounding);
    ClassDB::bind_method(D_METHOD("get_normal_rounding"), &IpropAsset::get_normal_rounding);
    ClassDB::bind_method(D_METHOD("set_normal_detail", "value"), &IpropAsset::set_normal_detail);
    ClassDB::bind_method(D_METHOD("get_normal_detail"), &IpropAsset::get_normal_detail);
    BIND_CONSTANT(IMAGE_NORMAL);
    BIND_CONSTANT(SOURCE_AUTHORED);
    BIND_CONSTANT(SOURCE_DERIVED);

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
    ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "resources"), "", "get_resources");
    ADD_PROPERTY(PropertyInfo(Variant::DICTIONARY, "work"), "", "get_work");
    ADD_PROPERTY(PropertyInfo(Variant::DICTIONARY, "last_error"), "", "get_last_error");
    ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "declared_images"), "", "get_declared_images");
    ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "images"), "", "get_images");
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "normal_image", PROPERTY_HINT_RESOURCE_TYPE, "Image"), "", "get_normal_image");
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "derive_normal"), "set_derive_normal", "get_derive_normal");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "normal_depth"), "set_normal_depth", "get_normal_depth");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "normal_rounding"), "set_normal_rounding", "get_normal_rounding");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "normal_detail"), "set_normal_detail", "get_normal_detail");
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
    // Replace shared containers; retained previous results must survive reload.
    outputs_ = Dictionary();
    part_outputs_ = Dictionary();
    output_slots_ = Array();
    resources_ = Array();
    work_ = Dictionary(); last_error_ = Dictionary();
    images_ = Array();
    normal_ = Ref<Image>();
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
    const Error result = setNamedInput(parameters_, name, value);
    if (result == OK) emit_changed();
    return result;
}

Array IpropAsset::get_declared_images() const {
    Array images;
    if (program_ == nullptr) return images;
    Failure failure;
    uint32_t count = 0;
    if (iprop_program_aux_image_count(program_, &count, failure.get()) != IPROP_EX_OK) {
        failure.print("image query");
        return images;
    }
    for (uint32_t i = 0; i < count; ++i) {
        iprop_aux_image_info_ex info;
        String name;
        if (iprop_aux_image_info_ex_init(&info, sizeof(info)) != IPROP_EX_OK ||
            iprop_program_aux_image_get(program_, i, &info, failure.get()) != IPROP_EX_OK ||
            !decodeName(info.name, name)) {
            failure.print("image query");
            return Array();
        }
        Dictionary entry;
        entry["index"] = static_cast<int64_t>(info.image_index);
        entry["kind"] = static_cast<int64_t>(info.kind);
        entry["name"] = name;
        images.append(entry);
    }
    return images;
}

void IpropAsset::set_derive_normal(bool enabled) {
    if (derive_normal_ == enabled) return;
    derive_normal_ = enabled;
    emit_changed();
}

void IpropAsset::set_normal_depth(double value) {
    if (normal_depth_ == value) return;
    normal_depth_ = value;
    emit_changed();
}

void IpropAsset::set_normal_rounding(double value) {
    if (normal_rounding_ == value) return;
    normal_rounding_ = value;
    emit_changed();
}

void IpropAsset::set_normal_detail(double value) {
    if (normal_detail_ == value) return;
    normal_detail_ = value;
    emit_changed();
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

Ref<Image> IpropAsset::generate(int64_t seed, int64_t width, int64_t height, const Dictionary& limits) {
    last_error_ = Dictionary();
    const auto invalid = [&](const char* message) -> Ref<Image> {
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
    if (width < 0 || height < 0 || width > UINT32_MAX || height > UINT32_MAX) {
        return invalid("width/height must be in 0..4294967295");
    }
    const auto validU32 = [](int64_t value) {
        return value >= 0 && value <= std::numeric_limits<uint32_t>::max();
    };
    if (!validU32(seed)) {
        return invalid("seed must fit u32");
    }
    if (program_ == nullptr) {
        return invalid("generate before load_file");
    }
    iprop_generate_options_ex options;
    iprop_generate_options_ex_init(&options, sizeof(options));
    options.flags = IPROP_GENERATE_PART_OUTPUTS;
    options.seed = static_cast<uint32_t>(seed);
    options.size_x = static_cast<uint32_t>(width);
    options.size_y = static_cast<uint32_t>(height);
    // The C API checks the request's ranges; a null request derives nothing.
    iprop_derived_normal_ex request;
    iprop_derived_normal_ex_init(&request, sizeof(request));
    request.depth = static_cast<float>(normal_depth_);
    request.rounding = static_cast<float>(normal_rounding_);
    request.detail = static_cast<float>(normal_detail_);
    Failure failure;
    iprop_result* result = nullptr;
    if (iprop_generate_derived_normal(program_, &options, parameters_, &execution,
                                      derive_normal_ ? &request : nullptr, &result, failure.get()) != IPROP_EX_OK) {
        last_error_ = failure.details();
        failure.print("generate");
        return Ref<Image>();
    }
    const std::unique_ptr<iprop_result, decltype(&iprop_result_free)> owned(result, iprop_result_free);
    const auto readFailure = [&]() {
        last_error_ = failure.details();
        if (failure.get()->status == IPROP_EX_OK) {
            last_error_["status"] = static_cast<int64_t>(IPROP_EX_E_INTERNAL);
            last_error_["reason"] = static_cast<int64_t>(IPROP_REASON_INTERNAL);
            last_error_["stage"] = static_cast<int64_t>(IPROP_STAGE_ARGUMENTS);
            last_error_["message"] = "unsupported result or allocation failure";
        }
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

    try {
    Dictionary candidate_outputs, candidate_parts;
    Array candidate_slots;
    uint32_t output_count;
    if (iprop_result_output_count(result, &output_count, failure.get()) != IPROP_EX_OK) return readFailure();
    for (uint32_t i = 0; i < output_count; ++i) {
        iprop_output_slot_ex slot;
        if (iprop::backend::querySlot(result, i, slot, failure.get()) != IPROP_EX_OK) return readFailure();
        String name;
        if (!decodeName(slot.name, name)) {
            return readFailure();
        }
        Dictionary entry;
        entry["kind"] = static_cast<int64_t>(slot.kind);
        entry["list"] = slot.is_list != 0;
        const auto components = iprop::backend::outputComponents(slot.kind);
        PackedFloat32Array values;
        Array pieces;
        if (slot.kind == IPROP_OUTPUT_KIND_PART) {
            std::vector<iprop_part_snapshot_ex> records;
            if (iprop::backend::copyParts(result, slot, records, failure.get()) != IPROP_EX_OK) return readFailure();
            for (const auto& p : records) {
                Dictionary value;
                if (!partValue(p, value)) return readFailure();
                pieces.append(value);
            }
        } else {
            if (components == 0) return readFailure();
            iprop_result_output_view_ex old;
            if (iprop::backend::queryOutput(result, i, old, failure.get()) != IPROP_EX_OK ||
                values.resize(static_cast<int64_t>(slot.element_count) * components) != OK ||
                iprop::backend::copyOutputValues(result, old, values.ptrw(), failure.get()) != IPROP_EX_OK) return readFailure();
        }
        entry["values"] = values;
        entry["parts"] = pieces;
        entry["count"] = static_cast<int64_t>(slot.element_count);
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
    std::vector<iprop::backend::ResultResource> stores;
    Array candidate_resources;
    if (iprop::backend::copyResources(result, stores, failure.get()) != IPROP_EX_OK ||
        !resourceValues(stores, candidate_resources)) return readFailure();
    iprop_work_stats_ex stats;
    if (iprop_work_stats_ex_init(&stats, sizeof(stats)) != IPROP_EX_OK ||
        iprop_result_query_work(result, &stats, failure.get()) != IPROP_EX_OK) return readFailure();
    Dictionary candidate_work;
    candidate_work["budget"] = static_cast<int64_t>(stats.work_budget);
    candidate_work["remaining"] = static_cast<int64_t>(stats.work_remaining);
    candidate_work["used"] = static_cast<int64_t>(stats.work_used);
    // Images published beside the main one, each an owned Godot Image.
    std::vector<iprop::backend::AuxImage> auxiliary;
    if (iprop::backend::copyAuxImages(result, auxiliary, failure.get()) != IPROP_EX_OK) return readFailure();
    Array candidate_images;
    Ref<Image> candidate_normal;
    for (const auto& item : auxiliary) {
        if (item.info.width > static_cast<uint32_t>(std::numeric_limits<int>::max()) ||
            item.info.height > static_cast<uint32_t>(std::numeric_limits<int>::max()) ||
            item.pixels.size() > static_cast<uint64_t>(std::numeric_limits<int64_t>::max())) return readFailure();
        PackedByteArray data;
        if (data.resize(static_cast<int64_t>(item.pixels.size())) != OK) return readFailure();
        if (!item.pixels.empty()) std::memcpy(data.ptrw(), item.pixels.data(), item.pixels.size());
        const Ref<Image> picture = Image::create_from_data(static_cast<int>(item.info.width),
            static_cast<int>(item.info.height), false, Image::FORMAT_RGBA8, data);
        if (picture.is_null() || picture->is_empty()) return readFailure();
        String name;
        if (!decodeName({item.name.data(), static_cast<uint32_t>(item.name.size())}, name)) return readFailure();
        Dictionary entry;
        entry["index"] = static_cast<int64_t>(item.info.image_index);
        entry["kind"] = static_cast<int64_t>(item.info.kind);
        entry["source"] = static_cast<int64_t>(item.info.source);
        entry["name"] = name;
        entry["image"] = picture;
        candidate_images.append(entry);
        if (candidate_normal.is_null() && item.info.kind == IPROP_AUX_IMAGE_NORMAL) candidate_normal = picture;
    }
    images_ = candidate_images;
    normal_ = candidate_normal;
    outputs_ = candidate_outputs;
    part_outputs_ = candidate_parts;
    output_slots_ = candidate_slots;
    resources_ = candidate_resources;
    work_ = candidate_work;
    return candidate_image;
    } catch (const std::exception&) { return readFailure(); }
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
    const Ref<ImageTexture> colour = ImageTexture::create_from_image(image);
    const Ref<Image> normal = asset_->get_normal_image();
    if (normal.is_null()) {
        set_texture(colour);
        return;
    }
    // Light2D nodes light a CanvasTexture through its normal map. Godot reads
    // normal maps as X+, Y+, Z+, which is how the runtime stores them.
    Ref<CanvasTexture> lit;
    lit.instantiate();
    lit->set_diffuse_texture(colour);
    lit->set_normal_texture(ImageTexture::create_from_image(normal));
    set_texture(lit);
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
