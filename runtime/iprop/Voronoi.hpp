#pragma once

#include "iprop/Geometry.hpp"

namespace iprop::detail {
// Internal constructors; VM opcodes own the public language contract.
struct VoronoiOptions {
    i32 count = 1, seed = 0, method = 0, relax = 0;
    f32 jitter = 1, anisotropy = 1;
    Vec2 direction{1,0};
    f32 rowBias = 0, rowJitter = 0, rowOffset = 0.5f;
};
std::shared_ptr<const GeometryData> generatedVoronoi(Execution& ex,Vec2 origin,Vec2 size,const VoronoiOptions& options);
std::shared_ptr<const GeometryData> explicitVoronoi(Execution& ex,Vec2 origin,Vec2 size,std::span<const Vec2> sites,i32 relax);
} // namespace iprop::detail
