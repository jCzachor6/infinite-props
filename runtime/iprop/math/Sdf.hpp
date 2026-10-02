#pragma once
#include "iprop/Types.hpp"

namespace iprop::math {

f32 distCircle(const Vec2& p, const Vec2& c, f32 r);
f32 distRect(const Vec2& p, const Vec2& c, const Vec2& half);
f32 distRoundRect(const Vec2& p, const Vec2& c, const Vec2& half, f32 radius);
f32 distLine(const Vec2& p, const Vec2& a, const Vec2& b);
f32 sdfFill(f32 distance, f32 width);

} // namespace iprop::math
