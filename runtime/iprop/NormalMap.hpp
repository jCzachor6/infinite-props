#pragma once

#include "iprop/Canvas.hpp"
#include "iprop/ExecutionLimits.hpp"

#include <string>

namespace iprop {

// An image published beside the main one (spec normal maps v1.1, material
// maps v1.2). The numeric values are the host tags of the C API. A result
// lists its images in this order, so a normal map is always the first.
enum class AuxImageKind : u32 { Normal = 1, Roughness = 2, Metallic = 3, Ao = 4, Emission = 5 };
inline constexpr u32 kAuxImageKinds = 5;
// The source spelling of a kind (`output roughness name`), or null.
constexpr const char* auxImageKindName(u32 kind) {
    constexpr const char* names[] = {"normal", "roughness", "metallic", "ao", "emission"};
    return kind >= 1 && kind <= kAuxImageKinds ? names[kind - 1] : nullptr;
}
// What a material map holds where its layer paints nothing: roughness and
// AO 255, metallic and emission 0 (material maps v1.2).
constexpr Color materialMapBackground(u32 kind) {
    return kind == static_cast<u32>(AuxImageKind::Roughness) || kind == static_cast<u32>(AuxImageKind::Ao)
        ? Color{255, 255, 255, 255} : Color{0, 0, 0, 255};
}
enum class AuxImageSource : u32 { Authored = 1, Derived = 2 };
struct AuxImage {
    AuxImageKind kind = AuxImageKind::Normal;
    AuxImageSource source = AuxImageSource::Authored;
    std::string name;
    // Tight top-left RGBA8 with the root canvas's dimensions and alpha 255
    // in every pixel. A normal image holds direction bytes; roughness,
    // metallic and AO hold one linear value in R, G and B; emission holds a
    // colour encoded like the main image's.
    Canvas image;
};

// Host request for a normal image derived from the finished image. A zero
// field selects its default, so a zeroed request is the recommended one.
struct DerivedNormalRequest {
    f32 depth = 0.f;    // height of full coverage, image pixels; default longer side / 16
    f32 rounding = 0.f; // Gaussian sigma, image pixels, at most 64; default longer side / 32
    f32 detail = 0.f;   // share of alpha-weighted brightness in the height, at most 1; default 0.5
};
inline constexpr f32 kDefaultDerivedNormalDetail = 0.5f;
// Whether every parameter is finite and in its documented range.
bool validDerivedNormalRequest(const DerivedNormalRequest& request) noexcept;
// The name of a derived normal image.
inline constexpr const char* kDerivedNormalName = "normal";

namespace detail {

// byte(v) of the direction encoding: floor(min(128 + 127.5*v, 255)) for
// v >= 0 and 255 - byte(-v) for v < 0.
u8 directionByte(double value);

// The derived normal image of a finished root image (spec normal maps v1.1,
// "Derived normal map"). Spends work only; throws Trap when its scratch
// would exceed 64 MiB.
Canvas deriveNormalImage(const Canvas& image, const DerivedNormalRequest& request, WorkBudget& work);

} // namespace detail
} // namespace iprop
