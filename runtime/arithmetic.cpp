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

// Spec §12.7: the pinned sine. Cody-Waite quadrant reduction and the Taylor
// polynomials on [-pi/4, pi/4], all in double precision. Every operation is
// a single IEEE round-to-nearest double op, so every backend reproduces the
// result bit for bit — this MUST NOT be replaced with the platform libm.
double sinCore(double x) {
    constexpr double PIO2 = 1.57079632679489661923e+00;    // pi/2 (double)
    constexpr double PIO2_HI = 1.57079632673412561417e+00; // 0x3FF921FB54400000
    constexpr double PIO2_LO = 6.07710050650619224932e-11; // 0x3DD0B4611A626331
    const double k = std::floor(x / PIO2 + 0.5); // the quadrant index
    const double r = (x - k * PIO2_HI) - k * PIO2_LO; // |r| <= pi/4

    // sin(r) and cos(r) in Horner form (the exact coefficients as written
    // in the spec; the compiler must not contract any mul+add).
    const double r2 = r * r;
    const double s =
        r * (1.0 + r2 * (-1.0 / 6.0 +
                         r2 * (1.0 / 120.0 +
                               r2 * (-1.0 / 5040.0 +
                                     r2 * (1.0 / 362880.0 +
                                           r2 * (-1.0 / 39916800.0))))));
    const double c =
        1.0 + r2 * (-0.5 +
                    r2 * (1.0 / 24.0 +
                          r2 * (-1.0 / 720.0 +
                                r2 * (1.0 / 40320.0 +
                                      r2 * (-1.0 / 3628800.0 +
                                            r2 * (1.0 / 479001600.0))))));

    // k mod 4, exact: the 0.25 scaling is a binary exponent shift and the
    // subtraction is exact for the small result.
    const double m = k - 4.0 * std::floor(k * 0.25);
    if (m == 0.0) {
        return s;
    }
    if (m == 1.0) {
        return c;
    }
    if (m == 2.0) {
        return -s;
    }
    return -c;
}

f32 f32Sin(f32 x) {
    const double value = static_cast<double>(x);
    if (std::isnan(value) || std::isinf(value)) {
        return std::numeric_limits<f32>::quiet_NaN();
    }
    return static_cast<f32>(sinCore(value));
}

f32 f32Cos(f32 x) {
    const double value = static_cast<double>(x);
    if (std::isnan(value) || std::isinf(value)) {
        return std::numeric_limits<f32>::quiet_NaN();
    }
    constexpr double PIO2 = 1.57079632679489661923e+00;
    return static_cast<f32>(sinCore(value + PIO2));
}

// ---- the pinned math package (spec §12.8) --------------------------------

constexpr double kLn2 = 6.93147180559945309417e-01;
constexpr double kLog2e = 1.44269504088896340736e+00;
constexpr double kPi = 3.14159265358979323846e+00;
constexpr double kPiOver2 = 1.57079632679489661923e+00;

// exp2 for a finite argument: n = floor(x), 2^x = e^(f*ln2) * 2^n with the
// Taylor series of degree 12 in Horner form. Everything in double; the
// caller handles NaN/infinity and casts to f32.
double exp2Core(double x) {
    double n = std::floor(x);
    // Keep the exponent within the ldexp-safe range; the out-of-range
    // cases overflow/underflow to +/-inf / 0, exactly as the f32 cast
    // would.
    if (n > 2047.0) {
        return std::numeric_limits<double>::infinity();
    }
    if (n < -2047.0) {
        return 0.0;
    }
    const double f = x - n;
    const double t = f * kLn2;
    double e = 1.0 / 479001600.0; // 1/12!
    e = e * t + 1.0 / 39916800.0; // 1/11!
    e = e * t + 1.0 / 3628800.0;  // 1/10!
    e = e * t + 1.0 / 362880.0;   // 1/9!
    e = e * t + 1.0 / 40320.0;    // 1/8!
    e = e * t + 1.0 / 5040.0;     // 1/7!
    e = e * t + 1.0 / 720.0;      // 1/6!
    e = e * t + 1.0 / 120.0;      // 1/5!
    e = e * t + 1.0 / 24.0;       // 1/4!
    e = e * t + 1.0 / 6.0;        // 1/3!
    e = e * t + 0.5;              // 1/2!
    e = e * t + 1.0;
    e = e * t + 1.0;
    return std::ldexp(e, static_cast<int>(n));
}



// log2 for a finite positive argument: frexp + the atanh series of
// v = u/(u+2), u = m-1, m in [1,2). The caller handles the edge cases.
double log2Core(double x) {
    int exponent = 0;
    const double m = std::frexp(x, &exponent); // x = m * 2^e, m in [0.5, 1)
    const double mm = m < 1.0 ? m * 2.0 : m;  // mm in [1, 2)
    const int e = m < 1.0 ? exponent - 1 : exponent;
    const double u = mm - 1.0;                // u in [0, 1)
    const double v = u / (u + 2.0);           // v in [0, 1/3)
    const double v2 = v * v;
    double g = 1.0 / 15.0;
    g = g * v2 + 1.0 / 13.0;
    g = g * v2 + 1.0 / 11.0;
    g = g * v2 + 1.0 / 9.0;
    g = g * v2 + 1.0 / 7.0;
    g = g * v2 + 1.0 / 5.0;
    g = g * v2 + 1.0 / 3.0;
    g = g * v2 + 1.0;
    g = g * v;
    return static_cast<double>(e) + 2.0 * g / kLn2;
}

f32 f32Exp(f32 x) {
    const double value = static_cast<double>(x);
    if (std::isnan(value)) {
        return std::numeric_limits<f32>::quiet_NaN();
    }
    if (value == std::numeric_limits<double>::infinity()) {
        return std::numeric_limits<f32>::infinity();
    }
    if (value == -std::numeric_limits<double>::infinity()) {
        return 0.0f;
    }
    return static_cast<f32>(exp2Core(value * kLog2e));
}

// atan(t) for t in [0, 1]: one half-angle reduction plus the Taylor
// series of degree 17 in Horner form.
double atanCore(double t) {
    const double z = t / (1.0 + std::sqrt(1.0 + t * t));
    const double z2 = z * z;
    // atan(z) = z * (1 - z^2/3 + z^4/5 - ... + z^16/17)
    double a = 1.0 / 17.0;
    a = a * z2 - 1.0 / 15.0;
    a = a * z2 + 1.0 / 13.0;
    a = a * z2 - 1.0 / 11.0;
    a = a * z2 + 1.0 / 9.0;
    a = a * z2 - 1.0 / 7.0;
    a = a * z2 + 1.0 / 5.0;
    a = a * z2 - 1.0 / 3.0;
    a = a * z2 + 1.0;
    a = a * z;
    return 2.0 * a;
}

f32 f32Atan2(f32 y, f32 x) {
    const double yy = static_cast<double>(y);
    const double xx = static_cast<double>(x);
    if (std::isnan(yy) || std::isnan(xx)) {
        return std::numeric_limits<f32>::quiet_NaN();
    }
    if (yy == 0.0) {
        const bool xPos = xx > 0.0 || xx == 0.0;
        return static_cast<f32>(std::copysign(xPos ? 0.0 : kPi, yy));
    }
    if (std::isinf(yy)) {
        if (xx == std::numeric_limits<double>::infinity()) {
            return static_cast<f32>(std::copysign(kPi / 4.0, yy));
        }
        if (xx == -std::numeric_limits<double>::infinity()) {
            return static_cast<f32>(std::copysign(3.0 * kPi / 4.0, yy));
        }
        return static_cast<f32>(std::copysign(kPiOver2, yy));
    }
    if (xx == std::numeric_limits<double>::infinity()) {
        return static_cast<f32>(std::copysign(0.0, yy));
    }
    if (xx == -std::numeric_limits<double>::infinity()) {
        return static_cast<f32>(yy >= 0.0 ? kPi : -kPi);
    }
    if (xx == 0.0) {
        return static_cast<f32>(yy >= 0.0 ? kPiOver2 : -kPiOver2);
    }
    const double ax = std::fabs(xx);
    const double ay = std::fabs(yy);
    double r = atanCore(ay / std::max(ax, ay));
    if (ay > ax) {
        r = kPiOver2 - r;
    }
    if (yy < 0.0) {
        r = -r;
    }
    if (xx < 0.0) {
        r = (yy >= 0.0 ? kPi : -kPi) - r;
    }
    return static_cast<f32>(r);
}

f32 f32Pow(f32 x, f32 y) {
    const double a = static_cast<double>(x);
    const double b = static_cast<double>(y);
    if (b == 0.0) {
        return 1.0f; // even when x is NaN
    }
    if (a == 1.0) {
        return 1.0f;
    }
    if (std::isnan(a) || std::isnan(b)) {
        return std::numeric_limits<f32>::quiet_NaN();
    }
    const bool yInteger = b == std::floor(b);
    const bool yOdd = yInteger && std::fmod(std::floor(b), 2.0) != 0.0;
    if (std::isinf(a)) {
        const bool neg = std::signbit(a);
        double magnitude =
            b > 0.0 ? std::numeric_limits<double>::infinity() : 0.0;
        if (neg && yOdd) {
            magnitude = std::copysign(magnitude, -1.0);
        }
        return static_cast<f32>(magnitude);
    }
    if (a == 0.0) {
        const bool neg = std::signbit(a);
        double magnitude =
            b > 0.0 ? 0.0 : std::numeric_limits<double>::infinity();
        if (neg && yOdd) {
            magnitude = std::copysign(magnitude, -1.0);
        }
        return static_cast<f32>(magnitude);
    }
    if (a < 0.0 && !yInteger) {
        return std::numeric_limits<f32>::quiet_NaN();
    }
    double r = exp2Core(b * log2Core(std::fabs(a)));
    if (a < 0.0 && yOdd) {
        r = -r;
    }
    return static_cast<f32>(r);
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
    case Opcode::F32Sin: {
        ex.push(Value(f32Sin(ex.popF32())));
        ++ex.frame().ip;
        return true;
    }
    case Opcode::F32Cos: {
        ex.push(Value(f32Cos(ex.popF32())));
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
        ex.push(Value(std::fmin(a, b)));
        ++ex.frame().ip;
        return true;
    }
    case Opcode::F32Max: {
        const f32 b = ex.popF32();
        const f32 a = ex.popF32();
        ex.push(Value(std::fmax(a, b)));
        ++ex.frame().ip;
        return true;
    }
    case Opcode::F32Pow: {
        const f32 b = ex.popF32();
        const f32 a = ex.popF32();
        ex.push(Value(f32Pow(a, b)));
        ++ex.frame().ip;
        return true;
    }
    case Opcode::F32Exp: {
        ex.push(Value(f32Exp(ex.popF32())));
        ++ex.frame().ip;
        return true;
    }
    case Opcode::F32Atan2: {
        const f32 b = ex.popF32(); // x
        const f32 a = ex.popF32(); // y
        ex.push(Value(f32Atan2(a, b)));
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
