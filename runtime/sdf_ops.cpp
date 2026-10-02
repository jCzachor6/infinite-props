#include "iprop/VmInternals.hpp"
#include "iprop/math/Sdf.hpp"
#include "iprop/math/Bezier.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace iprop::detail {

bool stepSdf(Execution& ex, Instruction ins) {
    switch (ins.opcode) {
    case Opcode::DistCircle: {
        const f32 r = ex.popF32();
        const Vec2 c = ex.popVec2();
        const Vec2 p = ex.popVec2();
        ex.push(Value(math::distCircle(p, c, r)));
        ++ex.frame().ip;
        return true;
    }
    case Opcode::DistRect: {
        const Vec2 half = ex.popVec2();
        const Vec2 c = ex.popVec2();
        const Vec2 p = ex.popVec2();
        ex.push(Value(math::distRect(p, c, half)));
        ++ex.frame().ip;
        return true;
    }
    case Opcode::DistRoundRect: {
        const f32 radius = ex.popF32();
        const Vec2 half = ex.popVec2();
        const Vec2 c = ex.popVec2();
        const Vec2 p = ex.popVec2();
        ex.push(Value(math::distRoundRect(p, c, half, radius)));
        ++ex.frame().ip;
        return true;
    }
    case Opcode::DistLine: {
        const Vec2 b = ex.popVec2();
        const Vec2 a = ex.popVec2();
        const Vec2 p = ex.popVec2();
        ex.push(Value(math::distLine(p, a, b)));
        ++ex.frame().ip;
        return true;
    }
    case Opcode::DistBezier: {
        const Vec2 c = ex.popVec2();
        const Vec2 b = ex.popVec2();
        const Vec2 a = ex.popVec2();
        const Vec2 p = ex.popVec2();
        ex.push(Value(math::distBezier(p, a, b, c)));
        ++ex.frame().ip;
        return true;
    }
    case Opcode::DistBezierCubic: {
        const Vec2 d = ex.popVec2();
        const Vec2 c = ex.popVec2();
        const Vec2 b = ex.popVec2();
        const Vec2 a = ex.popVec2();
        const Vec2 p = ex.popVec2();
        ex.push(Value(math::distBezier(p, a, b, c, d)));
        ++ex.frame().ip;
        return true;
    }
    case Opcode::Fill: {
        const f32 w = ex.popF32();
        const f32 d = ex.popF32();
        ex.push(Value(math::sdfFill(d, w)));
        ++ex.frame().ip;
        return true;
    }

    default: return false;
    }
}

} // namespace iprop::detail
