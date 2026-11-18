#pragma once

#include "iprop/Types.hpp"

#include <array>
#include <string_view>

namespace iprop {

// Wire mode 0 inherits the destination. A render target always holds an
// effective mode; an immutable image deliberately has no default blend.
enum class CompositionMode : u8 { Inherited = 0, Legacy = 1, SourceOver = 2, Multiply = 4, Screen, Overlay, Add, Subtract };
struct CompositionSpec {
    CompositionMode mode;
    std::string_view name;
    i32 sourceConstant;
};
inline constexpr std::array kCompositionSpecs{
    CompositionSpec{CompositionMode::Legacy, "legacy", 0},
    CompositionSpec{CompositionMode::SourceOver, "source_over", 1},
    CompositionSpec{CompositionMode::Multiply, "multiply", 4},
    CompositionSpec{CompositionMode::Screen, "screen", 5},
    CompositionSpec{CompositionMode::Overlay, "overlay", 6},
    CompositionSpec{CompositionMode::Add, "add", 7},
    CompositionSpec{CompositionMode::Subtract, "subtract", 8},
};
constexpr const CompositionSpec* compositionSpec(std::string_view name) {
    for (const auto& spec : kCompositionSpecs) if (spec.name == name) return &spec;
    return nullptr;
}
constexpr const CompositionSpec* compositionSpec(CompositionMode mode) {
    for (const auto& spec : kCompositionSpecs) if (spec.mode == mode) return &spec;
    return nullptr;
}
constexpr bool validCompositionMode(u16 mode) {
    if (mode == 0) return true;
    for (const auto& spec : kCompositionSpecs) if (mode == static_cast<u16>(spec.mode)) return true;
    return false;
}
// layer.begin alone also accepts a direction capture: source-over composition
// into a buffer whose RGB bytes are unit vectors (spec normal maps v1.1).
inline constexpr u8 kDirectionLayerMode = 3;
constexpr bool validLayerMode(u16 mode) {
    return mode <= kDirectionLayerMode;
}
// The aux byte of layer.begin: that mode in the low bits and, from the
// texture toolkit (v1.2), the capture's tiling in the two high ones. With
// neither bit a capture inherits the tiling of the active target. A 1.1
// runtime rejects either bit as an unknown layer mode.
inline constexpr u8 kLayerModeMask = 0x3F, kLayerTileOff = 0x40, kLayerTileOn = 0x80;
constexpr bool validLayerAux(u16 aux) {
    return aux <= 0xFF && validLayerMode(aux & kLayerModeMask) &&
        (aux & (kLayerTileOff | kLayerTileOn)) != (kLayerTileOff | kLayerTileOn);
}
constexpr CompositionMode resolveComposition(CompositionMode requested, CompositionMode active) {
    return requested == CompositionMode::Inherited ? active : requested;
}

// Straight RGBA8 in stored channel space. Each operation rounds and stores
// its result; no linear-light conversion or deferred premultiplied buffer.
// Inline: every raster kernel calls it once per written pixel.
inline Color composePixel(Color dst, Color src, CompositionMode mode) {
    if (mode == CompositionMode::SourceOver) {
        // The denominator is at most 255 * 255 and a numerator at most 255
        // times that, so every intermediate fits in 32 bits.
        const u32 sourceAlpha = src.a;
        const u32 destinationWeight = static_cast<u32>(dst.a) * (255u - sourceAlpha);
        const u32 denominator = sourceAlpha * 255u + destinationWeight;
        if (denominator == 0) return {};
        const auto channel = [&](u8 source, u8 destination) {
            const u32 numerator = static_cast<u32>(source) * sourceAlpha * 255u +
                static_cast<u32>(destination) * destinationWeight;
            return static_cast<u8>((numerator + denominator / 2u) / denominator);
        };
        return {channel(src.r, dst.r), channel(src.g, dst.g), channel(src.b, dst.b),
            static_cast<u8>((denominator + 127u) / 255u)};
    }
    // Frozen legacy kernel: opaque replacement, floor RGB division and
    // destination alpha preservation (including nonzero hidden RGB).
    if (src.a == 255) return src;
    const u32 a = src.a, b = 255u - a;
    return {static_cast<u8>((static_cast<u32>(src.r) * a + static_cast<u32>(dst.r) * b) / 255u),
        static_cast<u8>((static_cast<u32>(src.g) * a + static_cast<u32>(dst.g) * b) / 255u),
        static_cast<u8>((static_cast<u32>(src.b) * a + static_cast<u32>(dst.b) * b) / 255u), dst.a};
}

// Separable W3C blending in straight, stored channel space. Only the
// overlap uses B(Cb,Cs); a transparent backdrop retains the source colour.
inline Color composePaint(Color dst, Color src, CompositionMode mode) {
    if (mode < CompositionMode::Multiply) return composePixel(dst, src, mode);
    const u32 overlap = dst.a * src.a;
    const u32 sourceOnly = (255u - dst.a) * src.a;
    const u32 destOnly = dst.a * (255u - src.a);
    const u32 total = overlap + sourceOnly + destOnly;
    if (total == 0) return {};
    const auto channel = [&](u32 d, u32 s) -> u8 {
        u32 blended;
        switch (mode) {
        case CompositionMode::Multiply: blended = (d * s + 127) / 255; break;
        case CompositionMode::Screen: blended = 255 - ((255-d)*(255-s)+127)/255; break;
        case CompositionMode::Overlay: blended = d < 128 ? (2*d*s+127)/255 :
            255 - (2*(255-d)*(255-s)+127)/255; break;
        case CompositionMode::Add: blended = d+s > 255 ? 255 : d+s; break;
        default: blended = d > s ? d-s : 0; break;
        }
        return static_cast<u8>((blended*overlap + s*sourceOnly + d*destOnly + total/2)/total);
    };
    return {channel(dst.r,src.r), channel(dst.g,src.g), channel(dst.b,src.b),
            static_cast<u8>((total+127)/255)};
}

} // namespace iprop
