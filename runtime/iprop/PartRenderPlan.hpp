#pragma once

#include "iprop/Code.hpp"
#include "iprop/D4Transform.hpp"

namespace iprop {

// Prepared once per part call. Pure geometry, independent of VM state/fuel.
struct PartRenderPlan {
    D4Transform transform;
    Vec2 assetExtent;
    Vec2 sourceScale;
    RasterPoint sourceExtent;
    RasterPoint targetExtent;
    RasterPoint targetOrigin;
    u64 blitCost = 0;

    // Throws invalid_argument on invalid geometry, before any integer cast.
    static PartRenderPlan create(const Part& part, Vec2 callerScale,
                                 Vec2 placement, D4Transform transform);
};

} // namespace iprop
