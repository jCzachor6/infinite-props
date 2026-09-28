#include "iprop/VmInternals.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace iprop::detail {
namespace {

// Portable arithmetic right shift with two's-complement semantics
// (spec §9.3; C++ right shift of negatives is implementation-defined).
i32 shiftRightSigned(i32 value, u32 shift) {
    if (value >= 0) {
        return static_cast<i32>(static_cast<u32>(value) >> shift);
    }
    return static_cast<i32>(~((~static_cast<u32>(value)) >> shift));
}

// f32 -> i32 per spec §9.4: truncate toward zero, NaN -> 0, infinities and
// out-of-range values saturate.
i32 f32ToI32(f32 value) {
    if (std::isnan(value)) {
        return 0;
    }
    if (value <= -2147483648.f) { // exact in binary32
        return std::numeric_limits<i32>::min();
    }
    if (value >= 2147483648.f) { // 2^31; larger values saturate
        return std::numeric_limits<i32>::max();
    }
    return static_cast<i32>(value); // truncates toward zero
}

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

// Integer, float and colour arithmetic (spec §9.3-§9.5, determinism §13).
bool stepArithmetic(Execution& ex, Instruction ins) {
    switch (ins.opcode) {
    // --- Integer arithmetic (wraps in u32, spec §13.1) ---
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
        if (b == 0) ex.trap("divide by zero");
        if (a == std::numeric_limits<i32>::min() && b == -1) {
            ex.trap("integer overflow");
        }
        ex.push(Value(a / b));
        ++ex.frame().ip;
        return true;
    }
    case Opcode::I32DivU: {
        const u32 b = static_cast<u32>(ex.popI32());
        const u32 a = static_cast<u32>(ex.popI32());
        if (b == 0) ex.trap("divide by zero");
        ex.push(Value(static_cast<i32>(a / b)));
        ++ex.frame().ip;
        return true;
    }
    case Opcode::I32RemS: {
        const i32 b = ex.popI32();
        const i32 a = ex.popI32();
        if (b == 0) ex.trap("divide by zero");
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
        if (b == 0) ex.trap("divide by zero");
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
        ex.push(Value(shiftRightSigned(a, shift)));
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

    // --- Integer comparisons (spec §9.3) ---
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

    // --- Float arithmetic (spec §9.4; no FMA contraction, spec §13.2) ---
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
    case Opcode::I32ToF32: {
        ex.push(Value(static_cast<f32>(ex.popI32())));
        ++ex.frame().ip;
        return true;
    }
    case Opcode::F32ToI32: {
        ex.push(Value(f32ToI32(ex.popF32())));
        ++ex.frame().ip;
        return true;
    }

    // --- Colour (spec §9.5/§12.6) ---
    case Opcode::ColorMix: {
        const f32 t = std::clamp(ex.popF32(), 0.f, 1.f);
        const Color b = ex.popColor();
        const Color a = ex.popColor();
        // Explicit fused multiply-add: the QuantSpace reference builds
        // contract this expression, so the pinned formula reproduces its
        // exact output bit for bit (spec §12.6). std::fma is deterministic.
        const auto channel = [t](u8 x, u8 y) {
            return static_cast<u8>(std::fma(static_cast<f32>(x), 1.f - t,
                                            static_cast<f32>(y) * t));
        };
        ex.push(Value(Color{channel(a.r, b.r), channel(a.g, b.g), channel(a.b, b.b), a.a}));
        ++ex.frame().ip;
        return true;
    }
    case Opcode::ColorShade: {
        const i32 amount = ex.popI32();
        const Color c = ex.popColor();
        const auto channel = [amount](u8 x) {
            return static_cast<u8>(std::clamp(static_cast<int>(x) + amount, 0, 255));
        };
        ex.push(Value(Color{channel(c.r), channel(c.g), channel(c.b), c.a}));
        ++ex.frame().ip;
        return true;
    }
    case Opcode::ColorSetA: // reserved in v0.1 (spec §9.5)
        ex.trap("reserved opcode");

    default:
        return false;
    }
}

} // namespace iprop::detail
