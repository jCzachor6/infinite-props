#pragma once

#include "iprop/Types.hpp"

#include <vector>

namespace iprop {

// The target-pixel canvas (spec §8.1/§11): RGBA8, row-major, top-left
// origin, starting fully transparent. scaleX/scaleY map asset units to
// target pixels; rasterization multiplies coordinates by them (§11).
struct Canvas {
    u32 width = 0;
    u32 height = 0;
    f32 scaleX = 1.f;
    f32 scaleY = 1.f;
    std::vector<Color> pixels;

    void resize(u32 w, u32 h) {
        width = w;
        height = h;
        pixels.assign(static_cast<std::size_t>(w) * h, Color{0, 0, 0, 0});
    }

    Color& at(u32 x, u32 y) { return pixels[static_cast<std::size_t>(y) * width + x]; }
    const Color& at(u32 x, u32 y) const {
        return pixels[static_cast<std::size_t>(y) * width + x];
    }

    bool contains(i32 x, i32 y) const {
        return x >= 0 && y >= 0 && static_cast<u32>(x) < width && static_cast<u32>(y) < height;
    }
};

} // namespace iprop
