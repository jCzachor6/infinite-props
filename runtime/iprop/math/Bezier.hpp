#pragma once
#include "iprop/Types.hpp"

namespace iprop::math {

f32 distBezier(const Vec2& p, const Vec2& a, const Vec2& b, const Vec2& c);
f32 distBezier(const Vec2& p, const Vec2& a, const Vec2& b, const Vec2& c, const Vec2& d);

} // namespace iprop::math
