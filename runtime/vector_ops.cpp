#include "iprop/VmInternals.hpp"
#include "iprop/math/Vector.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace iprop::detail {

bool stepVector(Execution& ex, Instruction ins) {
    switch (ins.opcode) {
    case Opcode::Vec2Make: {
        const f32 y = ex.popF32();
        const f32 x = ex.popF32();
        ex.push(Value(Vec2{x, y}));
        ++ex.frame().ip;
        return true;
    }
    case Opcode::Vec2Add: {
        const Vec2 b = ex.popVec2();
        const Vec2 a = ex.popVec2();
        ex.push(Value(math::vec2Add(a, b)));
        ++ex.frame().ip;
        return true;
    }
    case Opcode::Vec2Sub: {
        const Vec2 b = ex.popVec2();
        const Vec2 a = ex.popVec2();
        ex.push(Value(math::vec2Sub(a, b)));
        ++ex.frame().ip;
        return true;
    }
    case Opcode::Vec2Mul: {
        const Value right = ex.pop();
        const Vec2 a = ex.popVec2();
        if (const Vec2* b = std::get_if<Vec2>(&right)) {
            ex.push(Value(math::vec2Mul(a, *b)));
        } else if (const f32* s = std::get_if<f32>(&right)) {
            ex.push(Value(math::vec2Mul(a, *s)));
        } else {
            ex.trap("stack type mismatch: expected f32 or vec2");
        }
        ++ex.frame().ip;
        return true;
    }
    case Opcode::Vec2Div: {
        const Value right = ex.pop();
        const Vec2 a = ex.popVec2();
        if (const Vec2* b = std::get_if<Vec2>(&right)) {
            ex.push(Value(math::vec2Div(a, *b)));
        } else if (const f32* s = std::get_if<f32>(&right)) {
            ex.push(Value(math::vec2Div(a, *s)));
        } else {
            ex.trap("stack type mismatch: expected f32 or vec2");
        }
        ++ex.frame().ip;
        return true;
    }
    case Opcode::Vec2Neg: {
        const Vec2 v = ex.popVec2();
        ex.push(Value(math::vec2Neg(v)));
        ++ex.frame().ip;
        return true;
    }
    case Opcode::Vec2Len: {
        const Vec2 v = ex.popVec2();
        ex.push(Value(math::vec2Len(v)));
        ++ex.frame().ip;
        return true;
    }
    case Opcode::Vec2Dot: {
        const Vec2 b = ex.popVec2();
        const Vec2 a = ex.popVec2();
        ex.push(Value(math::vec2Dot(a, b)));
        ++ex.frame().ip;
        return true;
    }
    case Opcode::Vec2Rot: {
        const f32 angle = ex.popF32();
        const Vec2 v = ex.popVec2();
        ex.push(Value(math::vec2Rot(v, angle)));
        ++ex.frame().ip;
        return true;
    }
    case Opcode::Vec2X: {
        ex.push(Value(ex.popVec2().x));
        ++ex.frame().ip;
        return true;
    }
    case Opcode::Vec2Y: {
        ex.push(Value(ex.popVec2().y));
        ++ex.frame().ip;
        return true;
    }
    case Opcode::Vec2Fold: {
        const i32 n = ex.popI32();
        const Vec2 v = ex.popVec2();
        ex.push(Value(math::vec2Fold(v, n)));
        ++ex.frame().ip;
        return true;
    }
    case Opcode::Vec2Polar: {
        ex.push(Value(math::vec2Polar(ex.popVec2())));
        ++ex.frame().ip;
        return true;
    }

    default: return false;
    }
}

} // namespace iprop::detail
