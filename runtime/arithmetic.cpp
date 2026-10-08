#include "iprop/VmInternals.hpp"
#include "iprop/math/Scalar.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace iprop::detail {
namespace {
// Shared shapes for the regular two-operand cases: pop, compute, push,
// advance. Special cases (division, shifts, conversions) stay inline.
void i32Binop(Execution& ex, u32 (*fn)(u32, u32)) {
    const u32 b = static_cast<u32>(ex.popI32());
    const u32 a = static_cast<u32>(ex.popI32());
    ex.push(Value(static_cast<i32>(fn(a, b))));
    ++ex.frame().ip;
}

void i32Compare(Execution& ex, i32 (*fn)(i32, i32)) {
    const i32 b = ex.popI32();
    const i32 a = ex.popI32();
    ex.push(Value(fn(a, b)));
    ++ex.frame().ip;
}

void f32Binop(Execution& ex, f32 (*fn)(f32, f32)) {
    const f32 b = ex.popF32();
    const f32 a = ex.popF32();
    ex.push(Value(fn(a, b)));
    ++ex.frame().ip;
}

void f32Compare(Execution& ex, i32 (*fn)(f32, f32)) {
    const f32 b = ex.popF32();
    const f32 a = ex.popF32();
    ex.push(Value(fn(a, b)));
    ++ex.frame().ip;
}

} // namespace


bool stepArithmetic(Execution& ex, Instruction ins) {
    switch (ins.opcode) {
    case Opcode::I32Add:
        i32Binop(ex, [](u32 a, u32 b) { return a + b; });
        return true;
    case Opcode::I32Sub:
        i32Binop(ex, [](u32 a, u32 b) { return a - b; });
        return true;
    case Opcode::I32Mul:
        i32Binop(ex, [](u32 a, u32 b) { return a * b; });
        return true;
    case Opcode::I32DivS: {
        const i32 b = ex.popI32();
        const i32 a = ex.popI32();
        if (b == 0) ex.trap("divide by zero", ErrorReason::DivideByZero);
        if (a == std::numeric_limits<i32>::min() && b == -1) {
            ex.trap("integer overflow", ErrorReason::IntegerOverflow);
        }
        ex.push(Value(a / b));
        ++ex.frame().ip;
        return true;
    }
    case Opcode::I32DivU: {
        const u32 b = static_cast<u32>(ex.popI32());
        const u32 a = static_cast<u32>(ex.popI32());
        if (b == 0) ex.trap("divide by zero", ErrorReason::DivideByZero);
        ex.push(Value(static_cast<i32>(a / b)));
        ++ex.frame().ip;
        return true;
    }
    case Opcode::I32RemS: {
        const i32 b = ex.popI32();
        const i32 a = ex.popI32();
        if (b == 0) ex.trap("divide by zero", ErrorReason::DivideByZero);
        if (a == std::numeric_limits<i32>::min() && b == -1) {
            ex.push(Value(i32{0}));
        } else {
            ex.push(Value(a % b));
        }
        ++ex.frame().ip;
        return true;
    }
    case Opcode::I32RemU: {
        const u32 b = static_cast<u32>(ex.popI32());
        const u32 a = static_cast<u32>(ex.popI32());
        if (b == 0) ex.trap("divide by zero", ErrorReason::DivideByZero);
        ex.push(Value(static_cast<i32>(a % b)));
        ++ex.frame().ip;
        return true;
    }
    case Opcode::I32And: {
        const i32 b = ex.popI32();
        const i32 a = ex.popI32();
        ex.push(Value(static_cast<i32>(static_cast<u32>(a) & static_cast<u32>(b))));
        ++ex.frame().ip;
        return true;
    }
    case Opcode::I32Or: {
        const i32 b = ex.popI32();
        const i32 a = ex.popI32();
        ex.push(Value(static_cast<i32>(static_cast<u32>(a) | static_cast<u32>(b))));
        ++ex.frame().ip;
        return true;
    }
    case Opcode::I32Xor: {
        const i32 b = ex.popI32();
        const i32 a = ex.popI32();
        ex.push(Value(static_cast<i32>(static_cast<u32>(a) ^ static_cast<u32>(b))));
        ++ex.frame().ip;
        return true;
    }
    case Opcode::I32Shl: {
        const u32 shift = static_cast<u32>(ex.popI32()) & 31u;
        const i32 a = ex.popI32();
        ex.push(Value(static_cast<i32>(static_cast<u32>(a) << shift)));
        ++ex.frame().ip;
        return true;
    }
    case Opcode::I32ShrS: {
        const u32 shift = static_cast<u32>(ex.popI32()) & 31u;
        const i32 a = ex.popI32();
        ex.push(Value(math::shiftRightSigned(a, shift)));
        ++ex.frame().ip;
        return true;
    }
    case Opcode::I32ShrU: {
        const u32 shift = static_cast<u32>(ex.popI32()) & 31u;
        const i32 a = ex.popI32();
        ex.push(Value(static_cast<i32>(static_cast<u32>(a) >> shift)));
        ++ex.frame().ip;
        return true;
    }
    case Opcode::I32Neg: {
        const i32 a = ex.popI32();
        ex.push(Value(static_cast<i32>(0u - static_cast<u32>(a))));
        ++ex.frame().ip;
        return true;
    }
    case Opcode::I32Not: {
        const i32 a = ex.popI32();
        ex.push(Value(static_cast<i32>(~static_cast<u32>(a))));
        ++ex.frame().ip;
        return true;
    }

    case Opcode::I32Eq:
        i32Compare(ex, [](i32 a, i32 b) { return a == b ? 1 : 0; });
        return true;
    case Opcode::I32Ne:
        i32Compare(ex, [](i32 a, i32 b) { return a != b ? 1 : 0; });
        return true;
    case Opcode::I32LtS:
        i32Compare(ex, [](i32 a, i32 b) { return a < b ? 1 : 0; });
        return true;
    case Opcode::I32LtU:
        i32Compare(ex, [](i32 a, i32 b) {
            return static_cast<u32>(a) < static_cast<u32>(b) ? 1 : 0;
        });
        return true;
    case Opcode::I32GtS:
        i32Compare(ex, [](i32 a, i32 b) { return a > b ? 1 : 0; });
        return true;
    case Opcode::I32GtU:
        i32Compare(ex, [](i32 a, i32 b) {
            return static_cast<u32>(a) > static_cast<u32>(b) ? 1 : 0;
        });
        return true;
    case Opcode::I32LeS:
        i32Compare(ex, [](i32 a, i32 b) { return a <= b ? 1 : 0; });
        return true;
    case Opcode::I32LeU:
        i32Compare(ex, [](i32 a, i32 b) {
            return static_cast<u32>(a) <= static_cast<u32>(b) ? 1 : 0;
        });
        return true;
    case Opcode::I32GeS:
        i32Compare(ex, [](i32 a, i32 b) { return a >= b ? 1 : 0; });
        return true;
    case Opcode::I32GeU:
        i32Compare(ex, [](i32 a, i32 b) {
            return static_cast<u32>(a) >= static_cast<u32>(b) ? 1 : 0;
        });
        return true;

    case Opcode::F32Add:
        f32Binop(ex, [](f32 a, f32 b) { return a + b; });
        return true;
    case Opcode::F32Sub:
        f32Binop(ex, [](f32 a, f32 b) { return a - b; });
        return true;
    case Opcode::F32Mul:
        f32Binop(ex, [](f32 a, f32 b) { return a * b; });
        return true;
    case Opcode::F32Div:
        f32Binop(ex, [](f32 a, f32 b) { return a / b; }); // IEEE: x/0 = inf
        return true;
    case Opcode::F32Eq:
        f32Compare(ex, [](f32 a, f32 b) { return a == b ? 1 : 0; }); // NaN: false
        return true;
    case Opcode::F32Ne:
        f32Compare(ex, [](f32 a, f32 b) { return a != b ? 1 : 0; }); // NaN: true
        return true;
    case Opcode::F32Lt:
        f32Compare(ex, [](f32 a, f32 b) { return a < b ? 1 : 0; });
        return true;
    case Opcode::F32Gt:
        f32Compare(ex, [](f32 a, f32 b) { return a > b ? 1 : 0; });
        return true;
    case Opcode::F32Le:
        f32Compare(ex, [](f32 a, f32 b) { return a <= b ? 1 : 0; });
        return true;
    case Opcode::F32Ge:
        f32Compare(ex, [](f32 a, f32 b) { return a >= b ? 1 : 0; });
        return true;
    case Opcode::F32Neg: {
        ex.push(Value(-ex.popF32()));
        ++ex.frame().ip;
        return true;
    }
    case Opcode::F32Abs: {
        ex.push(Value(std::fabs(ex.popF32())));
        ++ex.frame().ip;
        return true;
    }
    case Opcode::F32Sqrt: {
        ex.push(Value(std::sqrt(ex.popF32())));
        ++ex.frame().ip;
        return true;
    }
    case Opcode::F32Sin: {
        ex.push(Value(math::f32Sin(ex.popF32())));
        ++ex.frame().ip;
        return true;
    }
    case Opcode::F32Cos: {
        ex.push(Value(math::f32Cos(ex.popF32())));
        ++ex.frame().ip;
        return true;
    }
    case Opcode::F32Floor: {
        ex.push(Value(static_cast<f32>(std::floor(ex.popF32()))));
        ++ex.frame().ip;
        return true;
    }
    case Opcode::F32Ceil: {
        ex.push(Value(static_cast<f32>(std::ceil(ex.popF32()))));
        ++ex.frame().ip;
        return true;
    }
    case Opcode::F32Min: {
        const f32 b = ex.popF32();
        const f32 a = ex.popF32();
        ex.push(Value(math::f32MinMax(a, b, false)));
        ++ex.frame().ip;
        return true;
    }
    case Opcode::F32Max: {
        const f32 b = ex.popF32();
        const f32 a = ex.popF32();
        ex.push(Value(math::f32MinMax(a, b, true)));
        ++ex.frame().ip;
        return true;
    }
    case Opcode::F32Pow: {
        const f32 b = ex.popF32();
        const f32 a = ex.popF32();
        ex.push(Value(math::f32Pow(a, b)));
        ++ex.frame().ip;
        return true;
    }
    case Opcode::F32Exp: {
        ex.push(Value(math::f32Exp(ex.popF32())));
        ++ex.frame().ip;
        return true;
    }
    case Opcode::F32Atan2: {
        const f32 b = ex.popF32(); // x
        const f32 a = ex.popF32(); // y
        ex.push(Value(math::f32Atan2(a, b)));
        ++ex.frame().ip;
        return true;
    }
    case Opcode::I32ToF32: {
        ex.push(Value(static_cast<f32>(ex.popI32())));
        ++ex.frame().ip;
        return true;
    }
    case Opcode::F32ToI32: {
        ex.push(Value(math::f32ToI32(ex.popF32())));
        ++ex.frame().ip;
        return true;
    }


    default: return false;
    }
}

} // namespace iprop::detail
