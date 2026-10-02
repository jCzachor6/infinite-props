#include "iprop/VmInternals.hpp"
#include "iprop/math/Curves.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace iprop::detail {

bool stepCurves(Execution& ex, Instruction ins) {
    switch (ins.opcode) {
    case Opcode::Gain: {
        const f32 k = ex.popF32();
        const f32 t = ex.popF32();
        ex.push(Value(math::gainCurve(t, k)));
        ++ex.frame().ip;
        return true;
    }
    case Opcode::Quintic: {
        ex.push(Value(math::quinticCurve(ex.popF32())));
        ++ex.frame().ip;
        return true;
    }
    case Opcode::Smoothstep: {
        const f32 t = ex.popF32();
        const f32 b = ex.popF32();
        const f32 a = ex.popF32();
        ex.push(Value(math::smoothstepEdge(a, b, t)));
        ++ex.frame().ip;
        return true;
    }
    case Opcode::Remap: {
        const f32 d = ex.popF32();
        const f32 c = ex.popF32();
        const f32 b = ex.popF32();
        const f32 a = ex.popF32();
        const f32 t = ex.popF32();
        ex.push(Value(math::remapValue(t, a, b, c, d)));
        ++ex.frame().ip;
        return true;
    }
    case Opcode::Clamp: {
        const f32 b = ex.popF32();
        const f32 a = ex.popF32();
        const f32 t = ex.popF32();
        ex.push(Value(math::clampValue(t, a, b)));
        ++ex.frame().ip;
        return true;
    }

    case Opcode::Lerp: {
        const f32 t = ex.popF32();
        const f32 b = ex.popF32();
        const f32 a = ex.popF32();
        ex.push(Value(math::lerp(a, b, t)));
        ++ex.frame().ip;
        return true;
    }
    default: return false;
    }
}

} // namespace iprop::detail
