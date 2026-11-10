#pragma once

#include "iprop/Types.hpp"

#include <array>
#include <string_view>

namespace iprop {

// Wire mode 0 inherits the destination. A render target always holds an
// effective mode; an immutable image deliberately has no default blend.
enum class CompositionMode : u8 { Inherited = 0, Legacy = 1, SourceOver = 2 };
struct CompositionSpec {
    CompositionMode mode;
    std::string_view name;
    i32 sourceConstant;
};
inline constexpr std::array kCompositionSpecs{
    CompositionSpec{CompositionMode::Legacy, "legacy", 0},
    CompositionSpec{CompositionMode::SourceOver, "source_over", 1},
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

} // namespace iprop
