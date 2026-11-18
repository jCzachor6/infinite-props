#pragma once

#include "iprop/Constants.hpp"

#include <array>
#include <string_view>

namespace iprop {

// The texture page (texture toolkit v1.2): opcode 0x0E, whose aux byte is one
// of these operations or the byte of a noise or voronoi opcode. A noise
// there has the stack of its opcode with one more value on top, a period
// (vec2) or, for a noise mask, a tile flag (i32); a voronoi takes a tile
// flag too. The operand is the blend mode of a paint and zero everywhere
// else.
enum class TextureOp : u8 { PaintLayer, PaintMask, Gradient, Levels };
// How ipa and ipdis spell them: `texture paint 4`, `texture noise.fbm 0`.
inline constexpr std::array<std::string_view, 4> kTextureOpNames{"paint", "paint.mask", "gradient", "levels"};
// The fuel rate of a texture operation in percent, as the tariff gives one
// for an opcode: a paint that blends or fades a whole unit a pixel (it costs
// up to twice the CPU of surface.paint, which pays 60), the two lookup
// filters half a unit a pixel, a tiled noise mask above noise.mask for its
// wrapped lattice, and the periodic scalar noises their kernel units in
// full. Measured with benchmarks/operations/textures.py.
constexpr u8 textureFuelPercent(u8 aux) {
    switch (static_cast<TextureOp>(aux)) {
    case TextureOp::PaintLayer: case TextureOp::PaintMask: return 100;
    case TextureOp::Gradient: case TextureOp::Levels: return 50;
    default: break;
    }
    const auto base = static_cast<Opcode>(aux);
    // A tiled diagram runs the builder of its opcode on nine times the
    // sites, at that opcode's rate.
    if (base >= Opcode::Voronoi && base <= Opcode::VoronoiSitesRelax) return 20;
    return base == Opcode::NoiseMask || base == Opcode::NoiseMaskConfig ? 30 : 100;
}
constexpr bool periodicNoiseOpcode(Opcode op) {
    switch (op) {
    case Opcode::Noise: case Opcode::NoiseGrad: case Opcode::NoiseFbm:
    case Opcode::NoiseFbmConfig: case Opcode::NoiseRidge: case Opcode::NoiseCell:
    case Opcode::NoiseCell2: case Opcode::NoiseCellId: case Opcode::Warp:
    case Opcode::NoiseWarpVec: case Opcode::NoiseMask: case Opcode::NoiseMaskConfig:
        return true;
    default: return false;
    }
}
// The diagrams with a tiled form: every voronoi opcode. A tiled diagram has
// at most kMaxTiledSites cells, a ninth of the builder's sites.
constexpr bool tiledVoronoiOpcode(Opcode op) { return op >= Opcode::Voronoi && op <= Opcode::VoronoiSitesRelax; }
inline constexpr u32 kMaxTiledSites = 28;
// Every opcode whose byte is an operation of the page.
constexpr bool texturePageOpcode(Opcode op) { return periodicNoiseOpcode(op) || tiledVoronoiOpcode(op); }
} // namespace iprop
