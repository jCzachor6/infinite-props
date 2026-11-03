#pragma once

#include "iprop/Geometry.hpp"

namespace iprop::detail {
struct ContourOptions {
    f32 threshold = 0.5f, simplify = 0, minArea = 0;
    i32 connectivity = 0;
};
std::shared_ptr<const GeometryData> maskContours(Execution& ex, const MaskValue& source,
                                               const ContourOptions& options);
} // namespace iprop::detail
