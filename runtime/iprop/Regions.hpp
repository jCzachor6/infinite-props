#pragma once

#include "iprop/Geometry.hpp"

namespace iprop::detail {
// Conversions preserve the immutable store whenever no component is removed.
// Changed topology retains the original store for edge provenance.
std::shared_ptr<const GeometryData> geometryRegions(Execution& ex,
    const std::shared_ptr<const GeometryData>& source, f32 minArea = 0);
} // namespace iprop::detail
