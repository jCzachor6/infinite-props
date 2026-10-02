#pragma once
#include "iprop/math/MinMax.hpp"

namespace iprop::math {

i32 shiftRightSigned(i32 value, u32 shift);
i32 f32ToI32(f32 value);
f32 f32Sin(f32 value);
f32 f32Cos(f32 value);
f32 f32Exp(f32 value);
f32 f32Pow(f32 x, f32 y);
f32 f32Atan2(f32 y, f32 x);

} // namespace iprop::math
