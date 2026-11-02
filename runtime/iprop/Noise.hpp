#pragma once

#include "iprop/Types.hpp"
#include <array>
#include <string_view>

namespace iprop::detail {

// Internal scalar kernels shared by VM noise instructions and A8 generation.
// Callers reserve native work before sampling; these adapters do not charge.
struct Execution;
enum class NoiseMode : i32 { Value = 0, Grad = 1, Fbm = 2, Cell = 3, CellEdge = 4, Ridge = 5 };
inline constexpr std::array<std::string_view, 6> kNoiseModeNames{
    "mode_value", "mode_grad", "mode_fbm", "mode_cell", "mode_cell_edge", "mode_ridge"};
inline constexpr i32 kNoiseAbove = 0, kNoiseBelow = 1;
f32 sampleNoise(Execution& ex, NoiseMode kind, Vec2 point, i32 octaves, u32 seed);
Vec2 warpNoisePoint(Execution& ex, Vec2 point, f32 amount, u32 seed);

} // namespace iprop::detail
