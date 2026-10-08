#pragma once
#include "iprop/Types.hpp"

namespace iprop::math {

Color colorMix(Color a, Color b, f32 t);
// colorMix on all four channels, each kept between its two ends (spec v0.5 §5).
Color colorMixRgba(Color a, Color b, f32 t);
Color colorShade(Color c, i32 amount);
Color colorSetA(Color c, i32 amount);
Color colorMul(Color a, Color b);
Color colorScale(Color c, f32 scale);
// hueShift must be finite; the VM adapter validates this precondition.
Color colorHsv(const Color& c, f32 hueShift, f32 satScale, f32 valScale);
f32 ditherThreshold(f32 t, i32 x, i32 y);

} // namespace iprop::math
