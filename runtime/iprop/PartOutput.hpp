#pragma once

#include "iprop/Geometry.hpp"

namespace iprop {

inline constexpr u32 kMaxExportedParts = 256;

// An immutable source/view plus its effective source-pixel -> root-pixel
// affine mapping: linear D4, then integer translation. Geometry coordinates
// are multiplied by sourceScale first. Raster source/texture mappings stay
// in original source coordinates. No vertices or pixels are copied.
struct OutputPart {
    Part2dValue part;
    D4Transform transform;
    RasterPoint translation;
    Vec2 sourceScale, targetScale;
    Vec2 center, boundsOrigin, boundsSize;
    u32 index = 0; // append position in this slot, independent of instance ID
};

// Shared by result() copies. The generation charges the header and all
// reserved record capacity; results keep both records and sources alive.
struct PartOutputData {
    detail::ResourceVector<OutputPart, 128> records;
    explicit PartOutputData(ResourceArena& arena)
        : records(detail::ResourceAllocator<OutputPart, 128>(arena)) {}
};
static_assert(sizeof(OutputPart) <= 128);
static_assert(sizeof(PartOutputData) + 64 <= 128);

namespace detail {
struct Execution;
OutputPart snapshotPart(Execution& ex, const Part2dValue& part, u16 slot, u32 index);
}
} // namespace iprop
