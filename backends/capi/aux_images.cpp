#include "iprop.h"
#include "CheckedBoundary.hpp"
#include "Handles.hpp"

#include <cstddef>

// API 0.7: auxiliary images (spec normal maps v1.1); 0.8 adds the material
// maps (v1.2). Queries only: every image was published by the generation
// that produced the result.

static_assert(IPROP_AUX_IMAGE_NORMAL == static_cast<uint32_t>(iprop::AuxImageKind::Normal));
static_assert(IPROP_AUX_IMAGE_ROUGHNESS == static_cast<uint32_t>(iprop::AuxImageKind::Roughness));
static_assert(IPROP_AUX_IMAGE_METALLIC == static_cast<uint32_t>(iprop::AuxImageKind::Metallic));
static_assert(IPROP_AUX_IMAGE_AO == static_cast<uint32_t>(iprop::AuxImageKind::Ao));
static_assert(IPROP_AUX_IMAGE_EMISSION == static_cast<uint32_t>(iprop::AuxImageKind::Emission));
static_assert(IPROP_AUX_IMAGE_AUTHORED == static_cast<uint32_t>(iprop::AuxImageSource::Authored));
static_assert(IPROP_AUX_IMAGE_DERIVED == static_cast<uint32_t>(iprop::AuxImageSource::Derived));

namespace {
using namespace iprop::capi;

// The root's image outputs, at most one of each kind, in kind order: the
// order a result lists the images in.
uint32_t declaredCount(const iprop_program& program) noexcept {
    uint32_t count = 0;
    for (const auto& exported : program.data->resourceExports)
        if (exported.owner == iprop::kRootParent && exported.image) ++count;
    return count;
}
const iprop::ResourceExport* declaredImage(const iprop_program& program, uint32_t index) noexcept {
    for (uint32_t kind = 1; kind <= iprop::kAuxImageKinds; ++kind)
        for (const auto& exported : program.data->resourceExports)
            if (exported.owner == iprop::kRootParent && exported.image == kind && index-- == 0) return &exported;
    return nullptr;
}

template<class Descriptor>
void writeName(Descriptor* out, uint32_t capacity, const char* data, uint32_t byteCount) noexcept {
    writeField(out, capacity, offsetof(Descriptor, name) + offsetof(iprop_utf8_view, data), data);
    writeField(out, capacity, offsetof(Descriptor, name) + offsetof(iprop_utf8_view, byte_count), byteCount);
}
} // namespace

extern "C" {

iprop_status_ex iprop_aux_image_info_ex_init(iprop_aux_image_info_ex* out, uint32_t capacity) {
    if (out == nullptr || capacity < sizeof(uint32_t)) return IPROP_EX_E_ARGUMENT;
    writeField(out, capacity, 0, capacity);
    const iprop_aux_image_info_ex defaults{};
#define ZERO(field) writeField(out, capacity, offsetof(iprop_aux_image_info_ex, field), defaults.field)
    ZERO(flags); ZERO(image_index); ZERO(kind);
#undef ZERO
    writeName(out, capacity, defaults.name.data, defaults.name.byte_count);
    return IPROP_EX_OK;
}

iprop_status_ex iprop_aux_image_view_ex_init(iprop_aux_image_view_ex* out, uint32_t capacity) {
    if (out == nullptr || capacity < sizeof(uint32_t)) return IPROP_EX_E_ARGUMENT;
    writeField(out, capacity, 0, capacity);
    const iprop_aux_image_view_ex defaults{};
#define ZERO(field) writeField(out, capacity, offsetof(iprop_aux_image_view_ex, field), defaults.field)
    ZERO(flags); ZERO(image_index); ZERO(kind); ZERO(source); ZERO(width); ZERO(height);
    ZERO(byte_count); ZERO(row_stride); ZERO(pixel_format); ZERO(alpha_mode); ZERO(origin); ZERO(pixels);
#undef ZERO
    writeName(out, capacity, defaults.name.data, defaults.name.byte_count);
    return IPROP_EX_OK;
}

iprop_status_ex iprop_derived_normal_ex_init(iprop_derived_normal_ex* out, uint32_t capacity) {
    if (out == nullptr || capacity < sizeof(uint32_t)) return IPROP_EX_E_ARGUMENT;
    writeField(out, capacity, 0, capacity);
    const iprop_derived_normal_ex defaults{};
#define ZERO(field) writeField(out, capacity, offsetof(iprop_derived_normal_ex, field), defaults.field)
    ZERO(flags); ZERO(depth); ZERO(rounding); ZERO(detail);
#undef ZERO
    return IPROP_EX_OK;
}

iprop_status_ex iprop_program_aux_image_count(const iprop_program* program, uint32_t* out_count,
                                             iprop_error_ex* error) {
    ErrorContext context{iprop::ErrorStage::Arguments};
    return checkedBoundary(error, out_count, sizeof(uint32_t), context, [&] {
        if (program == nullptr) return CallOutcome::argument("program is null");
        if (out_count == nullptr) return CallOutcome::argument("out_count is null");
        *out_count = declaredCount(*program);
        return CallOutcome{};
    });
}

iprop_status_ex iprop_program_aux_image_get(const iprop_program* program, uint32_t image_index,
                                           iprop_aux_image_info_ex* out, iprop_error_ex* error) {
    ErrorContext context{iprop::ErrorStage::Arguments};
    return checkedBoundary(error, out, queryCapacity(out), context, [&] {
        if (program == nullptr) return CallOutcome::argument("program is null");
        uint32_t capacity = 0;
        if (!descriptorSize(out, capacity))
            return CallOutcome::argument("auxiliary image output is null or smaller than four bytes");
        if (readField<uint32_t>(out, capacity, offsetof(iprop_aux_image_info_ex, flags)) != 0)
            return CallOutcome::argument("auxiliary image flags are unsupported");
        const auto* declared = declaredImage(*program, image_index);
        if (declared == nullptr)
            return CallOutcome::argument("auxiliary image index is out of range");
        const auto& name = program->data->pool.strings[declared->nameIndex];
        const iprop_aux_image_kind kind = declared->image;
        writeField(out, capacity, offsetof(iprop_aux_image_info_ex, image_index), image_index);
        writeField(out, capacity, offsetof(iprop_aux_image_info_ex, kind), kind);
        writeName(out, capacity, name.data(), static_cast<uint32_t>(name.size()));
        return CallOutcome{};
    });
}

iprop_status_ex iprop_result_aux_image_count(const iprop_result* result, uint32_t* out_count,
                                            iprop_error_ex* error) {
    ErrorContext context{iprop::ErrorStage::Arguments};
    return checkedBoundary(error, out_count, sizeof(uint32_t), context, [&] {
        if (result == nullptr) return CallOutcome::argument("result is null");
        if (out_count == nullptr) return CallOutcome::argument("out_count is null");
        *out_count = static_cast<uint32_t>(result->images.size());
        return CallOutcome{};
    });
}

iprop_status_ex iprop_result_aux_image_get(const iprop_result* result, uint32_t image_index,
                                          iprop_aux_image_view_ex* out, iprop_error_ex* error) {
    ErrorContext context{iprop::ErrorStage::Arguments};
    return checkedBoundary(error, out, queryCapacity(out), context, [&] {
        if (result == nullptr) return CallOutcome::argument("result is null");
        uint32_t capacity = 0;
        if (!descriptorSize(out, capacity))
            return CallOutcome::argument("auxiliary image view is null or smaller than four bytes");
        if (readField<uint32_t>(out, capacity, offsetof(iprop_aux_image_view_ex, flags)) != 0)
            return CallOutcome::argument("auxiliary image view flags are unsupported");
        if (image_index >= result->images.size())
            return CallOutcome::argument("auxiliary image index is out of range");
        const auto& image = result->images[image_index];
        static_assert(sizeof(iprop::Color) == 4);
        iprop_aux_image_view_ex view{};
        view.image_index = image_index;
        view.kind = static_cast<iprop_aux_image_kind>(image.kind);
        view.source = static_cast<iprop_aux_image_source>(image.source);
        view.width = image.image.width;
        view.height = image.image.height;
        view.byte_count = uint64_t{image.image.pixels.size()} * sizeof(iprop::Color);
        view.row_stride = uint64_t{image.image.width} * sizeof(iprop::Color);
        view.pixel_format = IPROP_PIXEL_FORMAT_RGBA8;
        view.alpha_mode = IPROP_IMAGE_ALPHA_OPAQUE;
        view.origin = IPROP_IMAGE_ORIGIN_TOP_LEFT;
        view.pixels = image.image.pixels.empty() ? nullptr
            : reinterpret_cast<const uint8_t*>(image.image.pixels.data());
#define WRITE(field) writeField(out, capacity, offsetof(iprop_aux_image_view_ex, field), view.field)
        WRITE(image_index); WRITE(kind); WRITE(source); WRITE(width); WRITE(height);
        WRITE(byte_count); WRITE(row_stride); WRITE(pixel_format); WRITE(alpha_mode); WRITE(origin); WRITE(pixels);
#undef WRITE
        writeName(out, capacity, image.name.data(), static_cast<uint32_t>(image.name.size()));
        return CallOutcome{};
    });
}

iprop_status_ex iprop_result_aux_image_find(const iprop_result* result, iprop_aux_image_kind kind,
                                           uint32_t* out_index, iprop_error_ex* error) {
    ErrorContext context{iprop::ErrorStage::Arguments};
    return checkedBoundary(error, out_index, sizeof(uint32_t), context, [&] {
        if (result == nullptr) return CallOutcome::argument("result is null");
        if (out_index == nullptr) return CallOutcome::argument("out_index is null");
        for (std::size_t index = 0; index < result->images.size(); ++index) {
            if (static_cast<iprop_aux_image_kind>(result->images[index].kind) == kind) {
                *out_index = static_cast<uint32_t>(index);
                return CallOutcome{};
            }
        }
        return CallOutcome::argument("no auxiliary image of this kind was published");
    });
}

} // extern "C"
