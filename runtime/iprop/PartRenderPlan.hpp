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
    // The blit origin in the caller's asset units: targetOrigin / caller scale.
    Vec2 callerOffset;

    // Throws invalid_argument on invalid geometry, before any integer cast.
    static PartRenderPlan create(const Part& part, Vec2 callerScale,
                                 Vec2 placement, D4Transform transform);

    // A value of the part's asset space in its caller's asset space (spec
    // v0.5 §6): the D4 transform within the part, then the blit origin.
    Vec2 toCaller(Vec2 point) const;
    OutputRect toCaller(OutputRect rect) const;
};

} // namespace iprop
