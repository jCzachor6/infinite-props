#pragma once

#include "iprop/Constants.hpp"

#include <array>

namespace iprop::detail {

// Runtime 0.9 tariff; applies to old bytecode too. Kernel units stay explicit
// at their existing reservation sites. Round their sum once per instruction.
constexpr u8 fuelPercent(Opcode op) {
    switch (op) {
    case Opcode::MaskUnion: case Opcode::MaskIntersect:
    case Opcode::MaskSubtract: case Opcode::MaskInvert:
    case Opcode::MaskExpand: case Opcode::MaskShrink: return 40;
    case Opcode::LayerBlur: case Opcode::MaskBlur:
    case Opcode::SurfacePaint: case Opcode::SurfacePaintMask: return 60;
    case Opcode::Poly: case Opcode::PolyList: case Opcode::Oval:
    case Opcode::Curve: case Opcode::CurveCubic: return 75;
    case Opcode::PartCreateRegion: return 50;
    case Opcode::PartsFromRegions: return 25;
    case Opcode::NoiseMask: case Opcode::NoiseMaskConfig:
    case Opcode::PartCreateMask: case Opcode::PartCreateLayer:
    case Opcode::DynamicPartDraw: case Opcode::PartMask: return 20;
    default:
        if ((op >= Opcode::Voronoi && op <= Opcode::VoronoiSitesRelax) ||
            (op >= Opcode::Contour && op <= Opcode::RegionsContoursConfig) ||
            (op >= Opcode::RegionsMask && op <= Opcode::RegionsBevelConfig)) return 20;
        return 100;
    }
}

// The same tariff as a table: the interpreter reads one byte per instruction.
inline constexpr std::array<u8, 256> kFuelPercent = [] {
    std::array<u8, 256> table{};
    for (std::size_t op = 0; op < table.size(); ++op) table[op] = fuelPercent(static_cast<Opcode>(op));
    return table;
}();

// No product overflow, including UINT64_MAX. All percentages are in 1..100.
constexpr u64 fuelScaled(u64 units, u8 percent) {
    return units / 100 * percent + (units % 100 * percent + 99) / 100;
}

} // namespace iprop::detail
