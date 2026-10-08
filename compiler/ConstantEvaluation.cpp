#include "ConstantEvaluation.hpp"
#include "iprop/math/Color.hpp"
#include "iprop/math/Curves.hpp"
#include "iprop/math/Scalar.hpp"
#include "iprop/math/Vector.hpp"

#include <cmath>
#include <limits>

namespace iprop::compiler {
std::optional<Value> evaluateConstant(Opcode op, std::span<const Value> args) {
    const auto i = [&](std::size_t n) { return std::get<i32>(args[n]); };
    const auto u = [&](std::size_t n) { return static_cast<u32>(i(n)); };
    const auto f = [&](std::size_t n) { return std::get<f32>(args[n]); };
    const auto c = [&](std::size_t n) { return std::get<Color>(args[n]); };
    const auto v = [&](std::size_t n) { return std::get<Vec2>(args[n]); };
    const auto scalar = [](f32 value) -> std::optional<Value> {
        // Historical scalar NaN payloads are transported unchanged. Leave
        // non-finite arithmetic to the VM instead of folding host NaNs.
        return std::isfinite(value) ? std::optional<Value>(value) : std::nullopt;
    };
    switch (op) {
    case Opcode::I32Add: return static_cast<i32>(u(0) + u(1));
    case Opcode::I32Sub: return static_cast<i32>(u(0) - u(1));
    case Opcode::I32Mul: return static_cast<i32>(u(0) * u(1));
    case Opcode::I32And: return static_cast<i32>(u(0) & u(1));
    case Opcode::I32Or: return static_cast<i32>(u(0) | u(1));
    case Opcode::I32Xor: return static_cast<i32>(u(0) ^ u(1));
    case Opcode::I32Shl: return static_cast<i32>(u(0) << (u(1) & 31u));
    case Opcode::I32ShrU: return static_cast<i32>(u(0) >> (u(1) & 31u));
    case Opcode::I32ShrS: return math::shiftRightSigned(i(0), u(1) & 31u);
    case Opcode::I32Neg: return static_cast<i32>(0u - u(0));
    case Opcode::I32Not: return static_cast<i32>(~u(0));
    case Opcode::I32DivS:
        if (i(1) == 0 || (i(0) == std::numeric_limits<i32>::min() && i(1) == -1)) return std::nullopt;
        return i(0) / i(1);
    case Opcode::I32RemS:
        if (i(1) == 0) return std::nullopt;
        if (i(0) == std::numeric_limits<i32>::min() && i(1) == -1) return i32{0};
        return i(0) % i(1);
    case Opcode::I32Eq: return i32{i(0) == i(1)};
    case Opcode::I32Ne: return i32{i(0) != i(1)};
    case Opcode::I32LtS: return i32{i(0) < i(1)};
    case Opcode::I32LtU: return i32{u(0) < u(1)};
    case Opcode::I32LeS: return i32{i(0) <= i(1)};
    case Opcode::I32GtS: return i32{i(0) > i(1)};
    case Opcode::I32GeS: return i32{i(0) >= i(1)};
    case Opcode::I32ToF32: return static_cast<f32>(i(0));
    case Opcode::F32ToI32: return math::f32ToI32(f(0));
    case Opcode::F32Add: return scalar(f(0) + f(1));
    case Opcode::F32Sub: return scalar(f(0) - f(1));
    case Opcode::F32Mul: return scalar(f(0) * f(1));
    case Opcode::F32Div: return scalar(f(0) / f(1));
    case Opcode::F32Neg: return scalar(-f(0));
    case Opcode::F32Abs: return scalar(std::fabs(f(0)));
    case Opcode::F32Min: return scalar(math::f32MinMax(f(0), f(1), false));
    case Opcode::F32Max: return scalar(math::f32MinMax(f(0), f(1), true));
    case Opcode::Lerp: return scalar(math::lerp(f(0), f(1), f(2)));
    case Opcode::ColorMix: return math::colorMix(c(0), c(1), f(2));
    case Opcode::ColorShade: return math::colorShade(c(0), i(1));
    case Opcode::ColorSetA: return math::colorSetA(c(0), i(1));
    case Opcode::ColorMul: return math::colorMul(c(0), c(1));
    case Opcode::ColorMixRgba: return math::colorMixRgba(c(0), c(1), f(2));
    case Opcode::ColorScale: return math::colorScale(c(0), f(1));
    case Opcode::ColorHsv:
        if (!std::isfinite(f(1))) return std::nullopt; // preserve the runtime trap
        return math::colorHsv(c(0), f(1), f(2), f(3));
    case Opcode::ColorR: return i32{c(0).r};
    case Opcode::ColorG: return i32{c(0).g};
    case Opcode::ColorB: return i32{c(0).b};
    case Opcode::ColorA: return i32{c(0).a};
    case Opcode::Vec2Make: return Vec2{f(0), f(1)};
    case Opcode::Vec2Add: return math::vec2Add(v(0), v(1));
    case Opcode::Vec2Sub: return math::vec2Sub(v(0), v(1));
    case Opcode::Vec2Mul:
        if (const auto* b = std::get_if<Vec2>(&args[1])) return math::vec2Mul(v(0), *b);
        return math::vec2Mul(v(0), f(1));
    case Opcode::Vec2Div:
        if (const auto* b = std::get_if<Vec2>(&args[1])) return math::vec2Div(v(0), *b);
        return math::vec2Div(v(0), f(1));
    case Opcode::Vec2Neg: return math::vec2Neg(v(0));
    case Opcode::Vec2Len: return math::vec2Len(v(0));
    case Opcode::Vec2Dot: return math::vec2Dot(v(0), v(1));
    case Opcode::Vec2Rot: return math::vec2Rot(v(0), f(1));
    case Opcode::Vec2Fold: return math::vec2Fold(v(0), i(1));
    case Opcode::Vec2Polar: return math::vec2Polar(v(0));
    case Opcode::Vec2X: return v(0).x;
    case Opcode::Vec2Y: return v(0).y;
    default: return std::nullopt;
    }
}
} // namespace iprop::compiler
