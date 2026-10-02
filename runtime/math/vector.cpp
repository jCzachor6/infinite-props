#include "iprop/math/Vector.hpp"
#include "iprop/math/Scalar.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace iprop::math {

Vec2 vec2Rot(Vec2 v, f32 angle) {
    const f32 c = f32Cos(angle);
    const f32 s = f32Sin(angle);
    const f32 xc = v.x * c;
    const f32 ys = v.y * s;
    const f32 xs = v.x * s;
    const f32 yc = v.y * c;
    return canonical(Vec2{xc + ys, yc - xs});
}

Vec2 vec2Fold(const Vec2& v, i32 n) {
    n = std::clamp(n, 1, 64);
    const f32 r = std::sqrt(v.x * v.x + v.y * v.y);
    const f32 a = f32Atan2(v.y, v.x);
    const f32 sector =
        6.28318530717958647692f / static_cast<f32>(n); // TWO_PI
    const f32 quotient = a / sector + 0.5f;
    const f32 folded = a - std::floor(quotient) * sector;
    const f32 magnitude = std::fabs(folded);
    const f32 x = r * f32Cos(magnitude);
    const f32 y = r * f32Sin(magnitude);
    // A multiply may choose either operand's NaN sign/payload on different
    // backends. Canonicalize each component after the final products.
    return math::canonical(Vec2{x, y});
}

Vec2 vec2Polar(const Vec2& v) {
    return math::canonical(Vec2{std::sqrt(v.x * v.x + v.y * v.y), f32Atan2(-v.y, v.x)});
}

} // namespace iprop::math
