#pragma once
#include "iprop/math/Float.hpp"
#include <cmath>

namespace iprop::math {

inline Vec2 vec2Add(Vec2 a, Vec2 b) { return canonical(Vec2{a.x+b.x, a.y+b.y}); }
inline Vec2 vec2Sub(Vec2 a, Vec2 b) { return canonical(Vec2{a.x-b.x, a.y-b.y}); }
inline Vec2 vec2Mul(Vec2 a, Vec2 b) { return canonical(Vec2{a.x*b.x, a.y*b.y}); }
inline Vec2 vec2Mul(Vec2 a, f32 b) { return vec2Mul(a, Vec2{b,b}); }
inline Vec2 vec2Div(Vec2 a, Vec2 b) { return canonical(Vec2{a.x/b.x, a.y/b.y}); }
inline Vec2 vec2Div(Vec2 a, f32 b) { return vec2Div(a, Vec2{b,b}); }
inline Vec2 vec2Neg(Vec2 v) { return canonical(Vec2{-v.x,-v.y}); }
inline f32 vec2Len(Vec2 v) {
    const f32 xx = v.x*v.x;
    const f32 yy = v.y*v.y;
    return canonical(std::sqrt(xx+yy));
}
inline f32 vec2Dot(Vec2 a, Vec2 b) {
    const f32 abx = a.x*b.x;
    const f32 aby = a.y*b.y;
    return canonical(abx+aby);
}
Vec2 vec2Rot(Vec2 v, f32 angle);
Vec2 vec2Fold(const Vec2& v, i32 n);
Vec2 vec2Polar(const Vec2& v);

} // namespace iprop::math
