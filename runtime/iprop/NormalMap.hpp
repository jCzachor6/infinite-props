#pragma once

#include "iprop/Canvas.hpp"
#include "iprop/ExecutionLimits.hpp"

#include <string>

namespace iprop {

// An image published beside the main one (spec normal maps v1.1). The numeric
// values are the host tags of the C API.
enum class AuxImageKind : u32 { Normal = 1 };
enum class AuxImageSource : u32 { Authored = 1, Derived = 2 };
struct AuxImage {
    AuxImageKind kind = AuxImageKind::Normal;
    AuxImageSource source = AuxImageSource::Authored;
    std::string name;
    // Tight top-left RGBA8 with the root canvas's dimensions. A normal image
    // holds direction bytes and alpha 255 in every pixel.
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
