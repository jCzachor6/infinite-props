#pragma once

#include "iprop/Geometry.hpp"

namespace iprop::detail {
Part2dValue createPart(Execution& ex, const Regions2dValue& source, i32 index);
Part2dValue createPart(Execution& ex, const MaskValue& source);
Part2dValue createPart(Execution& ex, const LayerValue& source);
Parts2dValue partsFromRegions(Execution& ex, const Regions2dValue& source);
} // namespace iprop::detail
