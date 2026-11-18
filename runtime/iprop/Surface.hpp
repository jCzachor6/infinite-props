#pragma once

#include "iprop/Canvas.hpp"
#include "iprop/Compositor.hpp"
#include "iprop/D4Transform.hpp"

#include <memory>

namespace iprop {

// Parts and captures write the same RGBA8 storage. A closed capture exposes
// only a const buffer. Geometry and raster scale belong to the buffer; a view
// adds an integer placement and D4 without copying or resampling its pixels.
struct SurfaceBuffer {
    enum class Format { RGBA8, A8 };
    // What the RGB bytes of an RGBA8 buffer hold (spec normal maps v1.1). A
    // direction buffer stores unit vectors; painting it re-orients them with
    // the view's D4 kind. The encoding belongs to the buffer, not to a handle.
    enum class Encoding : u8 { Colour, Direction };
    Format format = Format::RGBA8;
    Encoding encoding = Encoding::Colour;
    Canvas raster;
    // A8 uses the same geometry, with one coverage byte instead of RGBA.
    std::vector<u8> coverage;
    // The raster is one period of an image that repeats in both directions
    // (texture toolkit v1.2): a capture with tiling, and what blur, normal,
    // gradient, levels and the mask operations make of one. Operations that
    // read the neighbours of a pixel wrap at its edges.
    bool tile = false;
};
struct SurfaceMapping {
    D4Transform transform;
    RasterPoint origin;
};
struct LayerValue {
    std::shared_ptr<const SurfaceBuffer> buffer;
    SurfaceMapping mapping;
};
struct MaskValue {
    std::shared_ptr<const SurfaceBuffer> buffer;
    SurfaceMapping mapping;
};
struct RenderTarget {
    std::shared_ptr<SurfaceBuffer> buffer;
    CompositionMode mode = CompositionMode::Legacy;
    std::vector<MaskValue> clips;
    bool tile = false;
    Canvas& canvas() { return buffer->raster; }
    const Canvas& canvas() const { return buffer->raster; }
    LayerValue view(SurfaceMapping mapping = {}) const { return {buffer, mapping}; }
    static RenderTarget create(u32 width, u32 height, Vec2 scale,
        CompositionMode mode = CompositionMode::Legacy, detail::WorkBudget* work = nullptr);
    static RenderTarget create(std::shared_ptr<SurfaceBuffer> buffer,
        u32 width, u32 height, Vec2 scale, CompositionMode mode, detail::WorkBudget* work = nullptr);
};

inline constexpr u32 kMaxSurfaceScopes = 32;
inline constexpr i32 kMaxMaskRadius = 64;
inline constexpr f32 kMaxBlurSigma = 64.0f;
inline constexpr f32 kMaxNormalSmoothing = 64.0f;

// The flat direction (0, 0, 1) and the background of a published normal image.
inline constexpr Color kFlatDirection{128, 128, 255, 255};

// A direction pixel seen through a D4 view: the linear part of the pixel
// mapping acts on (x, -y), and 255 - v negates a stored component exactly.
// The eight kinds act on the bytes as a group, like the transforms compose.
constexpr Color reorientDirection(Color pixel, u8 kind) {
    const auto negate = [](u8 value) { return static_cast<u8>(255u - value); };
    switch (kind) {
    case 1: return {negate(pixel.g), pixel.r, pixel.b, pixel.a};
    case 2: return {negate(pixel.r), negate(pixel.g), pixel.b, pixel.a};
    case 3: return {pixel.g, negate(pixel.r), pixel.b, pixel.a};
    case 4: return {negate(pixel.r), pixel.g, pixel.b, pixel.a};
    case 5: return {pixel.r, negate(pixel.g), pixel.b, pixel.a};
    case 6: return {negate(pixel.g), negate(pixel.r), pixel.b, pixel.a};
    case 7: return {pixel.g, pixel.r, pixel.b, pixel.a};
    default: return pixel;
    }
}
// Named captures include their handle/control metadata. Part canvases keep
// their existing 4 bytes per pixel, without the 128-byte handle charge.
inline constexpr u64 surfaceBytes(u32 width, u32 height) {
    return 128u + static_cast<u64>(width) * height * 4u;
}
inline constexpr u64 maskBytes(u32 width, u32 height) {
    return 128u + static_cast<u64>(width) * height;
}

// Multiplication at each coverage boundary uses nearest rounding, ties up.
constexpr u8 multiplyCoverage(u8 a, u8 b) {
    return static_cast<u8>((static_cast<u32>(a) * b + 127u) / 255u);
}

struct MaskBounds {
    i32 x = 0, y = 0;
    u32 width = 0, height = 0;
};
// The pixel rectangle a layer or mask view covers in target space; throws
// std::invalid_argument when it is not representable in i32 pixels.
MaskBounds surfaceBounds(const SurfaceBuffer& buffer, const SurfaceMapping& mapping);
MaskBounds maskBounds(const MaskValue& mask);
u8 maskCoverage(const MaskValue& mask, i64 x, i64 y);

} // namespace iprop
