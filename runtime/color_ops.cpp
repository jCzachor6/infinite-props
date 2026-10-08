#include "iprop/VmInternals.hpp"
#include "iprop/math/Color.hpp"
#include "iprop/math/Scalar.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace iprop::detail {

bool stepColor(Execution& ex, Instruction ins) {
    switch (ins.opcode) {
    case Opcode::ColorMix: {
        const f32 t = ex.popF32();
        const Color b = ex.popColor();
        const Color a = ex.popColor();
        ex.push(Value(math::colorMix(a, b, t)));
        ++ex.frame().ip;
        return true;
    }
    case Opcode::ColorShade: {
        const i32 amount = ex.popI32();
        const Color c = ex.popColor();
        ex.push(Value(math::colorShade(c, amount)));
        ++ex.frame().ip;
        return true;
    }
    case Opcode::ColorSetA: {
        const i32 amount = ex.popI32();
        const Color c = ex.popColor();
        ex.push(Value(math::colorSetA(c, amount)));
        ++ex.frame().ip;
        return true;
    }
    case Opcode::ColorMul: {
        const Color b = ex.popColor();
        const Color a = ex.popColor();
        ex.push(Value(math::colorMul(a, b)));
        ++ex.frame().ip;
        return true;
    }
    case Opcode::ColorScale: {
        const f32 s = ex.popF32();
        const Color c = ex.popColor();
        ex.push(Value(math::colorScale(c, s)));
        ++ex.frame().ip;
        return true;
    }
    case Opcode::ColorHsv: {
        const f32 valScale = ex.popF32();
        const f32 satScale = ex.popF32();
        const f32 hueShift = ex.popF32();
        const Color c = ex.popColor();
        if (!std::isfinite(hueShift)) {
            ex.trap("color.hsv hue shift must be finite", ErrorReason::NumericRange);
        }
        ex.push(Value(math::colorHsv(c, hueShift, satScale, valScale)));
        ++ex.frame().ip;
        return true;
    }
    case Opcode::Dither: {
        const i32 y = ex.popI32();
        const i32 x = ex.popI32();
        const f32 t = ex.popF32();
        ex.push(Value(math::ditherThreshold(t, x, y)));
        ++ex.frame().ip;
        return true;
    }

    default: return false;
    }
}

} // namespace iprop::detail
