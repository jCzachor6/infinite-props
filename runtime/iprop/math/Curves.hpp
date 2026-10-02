#pragma once
#include "iprop/Types.hpp"

namespace iprop::math {

// Same polynomial for noise fade and the clamped quintic API; range handling
// belongs to each caller. Written order is part of the deterministic contract.
inline f32 quinticPolynomial(f32 t) {
    const f32 t2 = t * t;
    const f32 t3 = t2 * t;
    const f32 inner = t * (t * 6.f - 15.f) + 10.f;
    return t3 * inner;
}
f32 gainCurve(f32 t, f32 k);
f32 quinticCurve(f32 t);
f32 clampValue(f32 t, f32 a, f32 b);
f32 smoothstepEdge(f32 a, f32 b, f32 t);
f32 remapValue(f32 t, f32 a, f32 b, f32 c, f32 d);
f32 lerp(f32 a, f32 b, f32 t);

} // namespace iprop::math
