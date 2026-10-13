#pragma once

#include "iprop/Canvas.hpp"
#include "iprop/D4Transform.hpp"

#include <memory>

namespace iprop {

// Parts and captures write the same RGBA8 storage. A closed capture exposes
// only a const buffer. Geometry and raster scale belong to the buffer; a view
// adds an integer placement and D4 without copying or resampling its pixels.
struct SurfaceBuffer {
    Canvas raster;
};
struct SurfaceMapping {
    D4Transform transform;
    RasterPoint origin;
};
struct LayerValue {
    std::shared_ptr<const SurfaceBuffer> buffer;
    SurfaceMapping mapping;
};
struct RenderTarget {
    std::shared_ptr<SurfaceBuffer> buffer;
    Canvas& canvas() { return buffer->raster; }
    const Canvas& canvas() const { return buffer->raster; }
    LayerValue view(SurfaceMapping mapping = {}) const { return {buffer, mapping}; }
    static RenderTarget create(u32 width, u32 height, Vec2 scale);
};

inline constexpr u32 kMaxSurfaceScopes = 32;
// Named captures include their handle/control metadata. Part canvas accounting
// retains its existing 4 bytes per pixel in format 0.6, and older files remain
// outside the generation arena.
inline constexpr u64 surfaceBytes(u32 width, u32 height) {
    return 128u + static_cast<u64>(width) * height * 4u;
}

} // namespace iprop
