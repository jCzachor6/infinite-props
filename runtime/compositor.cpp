#include "iprop/Compositor.hpp"

namespace iprop {

Color composePixel(Color dst, Color src, CompositionMode mode) {
    if (mode == CompositionMode::SourceOver) {
        const u64 sourceAlpha = src.a;
        const u64 destinationWeight = static_cast<u64>(dst.a) * (255u - sourceAlpha);
        const u64 denominator = sourceAlpha * 255u + destinationWeight;
        if (denominator == 0) return {};
        const auto channel = [&](u8 source, u8 destination) {
            const u64 numerator = static_cast<u64>(source) * sourceAlpha * 255u +
                static_cast<u64>(destination) * destinationWeight;
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
