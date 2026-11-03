#pragma once

#include "iprop/Geometry.hpp"

namespace iprop::detail {
// Conversions preserve the immutable store whenever no component is removed.
// Changed topology retains the original store for edge provenance.
std::shared_ptr<const GeometryData> geometryRegions(Execution& ex,
    const std::shared_ptr<const GeometryData>& source, f32 minArea = 0);
// The same offset kernel is reused by constant gaps; no public inset method.
std::shared_ptr<const GeometryData> offsetRegions(Execution& ex,
    const std::shared_ptr<const GeometryData>& source, f32 inset, f32 minArea = 0);
// Shared raster pipeline for a complete union (-1) or a single component.
// It samples in the active raster scale, independently of clipping and target extent.
MaskValue rasterRegions(Execution& ex, const GeometryData& source, i32 index = -1);
} // namespace iprop::detail
