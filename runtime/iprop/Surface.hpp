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
    Format format = Format::RGBA8;
    Canvas raster;
    // A8 uses the same geometry, with one coverage byte instead of RGBA.
    std::vector<u8> coverage;
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
    Canvas& canvas() { return buffer->raster; }
    const Canvas& canvas() const { return buffer->raster; }
    LayerValue view(SurfaceMapping mapping = {}) const { return {buffer, mapping}; }
    static RenderTarget create(u32 width, u32 height, Vec2 scale,
        CompositionMode mode = CompositionMode::Legacy);
    static RenderTarget create(std::shared_ptr<SurfaceBuffer> buffer,
        u32 width, u32 height, Vec2 scale, CompositionMode mode);
};

inline constexpr u32 kMaxSurfaceScopes = 32;
inline constexpr i32 kMaxMaskRadius = 64;
inline constexpr f32 kMaxBlurSigma = 64.0f;
// Named captures include their handle/control metadata. Part canvas accounting
// retains its existing 4 bytes per pixel in format 0.6, and older files remain
// outside the generation arena.
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
MaskBounds maskBounds(const MaskValue& mask);
u8 maskCoverage(const MaskValue& mask, i64 x, i64 y);

} // namespace iprop
