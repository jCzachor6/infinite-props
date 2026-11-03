#pragma once

#include "iprop/Geometry.hpp"

namespace iprop::detail {
Part2dValue createPart(Execution& ex, const Regions2dValue& source, i32 index);
Part2dValue createPart(Execution& ex, const MaskValue& source);
Part2dValue createPart(Execution& ex, const LayerValue& source);
Parts2dValue partsFromRegions(Execution& ex, const Regions2dValue& source);
Part2dValue placePart(Execution& ex, const Part2dValue& p, Vec2 at, i32 rot, i32 mirrorX, i32 mirrorY);
Part2dValue fillPart(Execution& ex, const Part2dValue& p, Color tint);
Part2dValue texturePart(Execution& ex, const Part2dValue& p, const LayerValue& image);
// Compose the original raster view with the instance's absolute placement.
SurfaceMapping partSurfaceMapping(Execution& ex, const DynamicPartData& p,
    const SurfaceBuffer& source, const SurfaceMapping& mapping);
} // namespace iprop::detail
