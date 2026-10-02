#include "ips/BuiltinSpec.hpp"

namespace iprop::ips {
const std::map<std::string, BuiltinSpec>& builtinSpecs() {
    using V = ValueType;
    using L = BuiltinLowering;
    static const std::map<std::string, BuiltinSpec> table = {
        {"unit", {V::F32, L::Opcode, {{{}, Opcode::RngUnit}}}},
        {"stream_push", {V::Void, L::Opcode, {{{{"seed", V::I32}}, Opcode::StreamPush}}}},
        {"stream_pop", {V::Void, L::Opcode, {{{}, Opcode::StreamPop}}}},
        {"range", {V::F32, L::Opcode, {{{{"a", V::F32}, {"b", V::F32}}, Opcode::RngRange}}}},
        {"index", {V::I32, L::Opcode, {{{{"n", V::I32}}, Opcode::RngIndex}}}},
        {"pick", {V::I32, L::Pick, {{{{"n", V::I32}}, Opcode::RngIndex}}}},
        {"chance", {V::I32, L::Chance, {{{{"p", V::F32}}, std::nullopt}}}},
        {"hash", {V::I32, L::Opcode, {{{{"x", V::I32}}, Opcode::Hash}}}},
        {"noise", {V::F32, L::Opcode, {{{{"x", V::F32}, {"y", V::F32}, {"seed", V::I32}}, Opcode::Noise}}}},
        {"fault", {V::F32, L::Opcode, {{{{"x", V::F32}, {"y", V::F32}, {"seed", V::I32}}, Opcode::Fault}}}},
        {"smooth", {V::F32, L::Opcode, {{{{"t", V::F32}}, Opcode::Smooth}}}},
        {"sqrt", {V::F32, L::Opcode, {{{{"x", V::F32}}, Opcode::F32Sqrt}}}},
        {"sin", {V::F32, L::Opcode, {{{{"x", V::F32}}, Opcode::F32Sin}}}},
        {"cos", {V::F32, L::Opcode, {{{{"x", V::F32}}, Opcode::F32Cos}}}},
        {"abs", {V::F32, L::Opcode, {{{{"x", V::F32}}, Opcode::F32Abs}}}},
        {"floor", {V::F32, L::Opcode, {{{{"x", V::F32}}, Opcode::F32Floor}}}},
        {"ceil", {V::F32, L::Opcode, {{{{"x", V::F32}}, Opcode::F32Ceil}}}},
        {"min", {V::F32, L::Opcode, {{{{"x", V::F32}, {"y", V::F32}}, Opcode::F32Min}}}},
        {"max", {V::F32, L::Opcode, {{{{"x", V::F32}, {"y", V::F32}}, Opcode::F32Max}}}},
        {"pow", {V::F32, L::Opcode, {{{{"x", V::F32}, {"y", V::F32}}, Opcode::F32Pow}}}},
        {"exp", {V::F32, L::Opcode, {{{{"x", V::F32}}, Opcode::F32Exp}}}},
        {"atan2", {V::F32, L::Opcode, {{{{"y", V::F32}, {"x", V::F32}}, Opcode::F32Atan2}}}},
        {"dist.circle", {V::F32, L::Opcode, {{{{"p", V::Vec2}, {"center", V::Vec2}, {"radius", V::F32}}, Opcode::DistCircle}}}},
        {"dist.rect", {V::F32, L::Opcode, {{{{"p", V::Vec2}, {"center", V::Vec2}, {"half_size", V::Vec2}}, Opcode::DistRect}}}},
        {"dist.roundrect", {V::F32, L::Opcode, {{{{"p", V::Vec2}, {"center", V::Vec2}, {"half_size", V::Vec2}, {"radius", V::F32}}, Opcode::DistRoundRect}}}},
        {"dist.line", {V::F32, L::Opcode, {{{{"p", V::Vec2}, {"a", V::Vec2}, {"b", V::Vec2}}, Opcode::DistLine}}}},
        {"dist.bezier", {V::F32, L::Opcode, {
            {{{"p", V::Vec2}, {"a", V::Vec2}, {"b", V::Vec2}, {"c", V::Vec2}}, Opcode::DistBezier},
            {{{"p", V::Vec2}, {"a", V::Vec2}, {"b", V::Vec2}, {"c", V::Vec2}, {"d", V::Vec2}}, Opcode::DistBezierCubic}}}},
        {"fill", {V::F32, L::Opcode, {{{{"d", V::F32}, {"w", V::F32}}, Opcode::Fill}}}},
        {"gain", {V::F32, L::Opcode, {{{{"t", V::F32}, {"k", V::F32}}, Opcode::Gain}}}},
        {"quintic", {V::F32, L::Opcode, {{{{"t", V::F32}}, Opcode::Quintic}}}},
        {"noise.grad", {V::F32, L::Opcode, {{{{"x", V::F32}, {"y", V::F32}, {"seed", V::I32}}, Opcode::NoiseGrad}}}},
        {"noise.fbm", {V::F32, L::Opcode, {{{{"x", V::F32}, {"y", V::F32}, {"octaves", V::I32}, {"seed", V::I32}}, Opcode::NoiseFbm}}}},
        {"noise.cell", {V::F32, L::Opcode, {{{{"x", V::F32}, {"y", V::F32}, {"seed", V::I32}}, Opcode::NoiseCell}}}},
        {"warp", {V::F32, L::Opcode, {{{{"x", V::F32}, {"y", V::F32}, {"amount", V::F32}, {"seed", V::I32}}, Opcode::Warp}}}},
        {"cell.jitter", {V::Vec2, L::Opcode, {{{{"cx", V::I32}, {"cy", V::I32}, {"seed", V::I32}}, Opcode::CellJitter}}}},
        {"noise.ridge", {V::F32, L::Opcode, {{{{"x", V::F32}, {"y", V::F32}, {"octaves", V::I32}, {"seed", V::I32}}, Opcode::NoiseRidge}}}},
        {"color.set.a", {V::Color, L::Opcode, {{{{"color", V::Color}, {"alpha", V::I32}}, Opcode::ColorSetA}}}},
        {"color.mul", {V::Color, L::Opcode, {{{{"a", V::Color}, {"b", V::Color}}, Opcode::ColorMul}}}},
        {"color.scale", {V::Color, L::Opcode, {{{{"color", V::Color}, {"scale", V::F32}}, Opcode::ColorScale}}}},
        {"color.hsv", {V::Color, L::Opcode, {{{{"color", V::Color}, {"hue", V::F32}, {"saturation", V::F32}, {"value", V::F32}}, Opcode::ColorHsv}}}},
        {"dither", {V::F32, L::Opcode, {{{{"t", V::F32}, {"x", V::I32}, {"y", V::I32}}, Opcode::Dither}}}},
        {"fold", {V::Vec2, L::Opcode, {{{{"v", V::Vec2}, {"n", V::I32}}, Opcode::Vec2Fold}}}},
        {"polar", {V::Vec2, L::Opcode, {{{{"v", V::Vec2}}, Opcode::Vec2Polar}}}},
        {"vec2.len", {V::F32, L::Opcode, {{{{"v", V::Vec2}}, Opcode::Vec2Len}}}},
        {"vec2.dot", {V::F32, L::Opcode, {{{{"a", V::Vec2}, {"b", V::Vec2}}, Opcode::Vec2Dot}}}},
        {"vec2.rot", {V::Vec2, L::Opcode, {{{{"v", V::Vec2}, {"a", V::F32}}, Opcode::Vec2Rot}}}},
        {"smoothstep", {V::F32, L::Opcode, {{{{"a", V::F32}, {"b", V::F32}, {"t", V::F32}}, Opcode::Smoothstep}}}},
        {"remap", {V::F32, L::Opcode, {{{{"t", V::F32}, {"a", V::F32}, {"b", V::F32}, {"c", V::F32}, {"d", V::F32}}, Opcode::Remap}}}},
        {"clamp", {V::F32, L::Opcode, {{{{"x", V::F32}, {"min", V::F32}, {"max", V::F32}}, Opcode::Clamp}}}},
        {"i32trunc", {V::I32, L::Opcode, {{{{"x", V::F32}}, Opcode::F32ToI32}}}},
        {"int", {V::I32, L::Opcode, {
            {{{"x", V::I32}}, std::nullopt}, {{{"x", V::F32}}, Opcode::F32ToI32}}}},
        {"float", {V::F32, L::Opcode, {
            {{{"x", V::F32}}, std::nullopt}, {{{"x", V::I32}}, Opcode::I32ToF32}}}},
        {"rgb", {V::Color, L::ColorConstructor, {{{{"r", V::I32}, {"g", V::I32}, {"b", V::I32}}, std::nullopt}}}},
        {"rgba", {V::Color, L::ColorConstructor, {{{{"r", V::I32}, {"g", V::I32}, {"b", V::I32}, {"a", V::I32}}, std::nullopt}}}},
        {"round", {V::I32, L::Round, {{{{"x", V::F32}}, std::nullopt}}}},
        {"lerp", {L::Opcode, {
            {{{"a", V::F32}, {"b", V::F32}, {"t", V::F32}}, Opcode::Lerp, V::F32},
            {{{"a", V::Color}, {"b", V::Color}, {"t", V::F32}}, Opcode::ColorMix, V::Color}}}},
        {"color.mix", {V::Color, L::Opcode, {{{{"a", V::Color}, {"b", V::Color}, {"t", V::F32}}, Opcode::ColorMix}}}},
        {"color.shade", {V::Color, L::Opcode, {{{{"color", V::Color}, {"amount", V::I32}}, Opcode::ColorShade}}}},
        {"pixel", {V::Void, L::Opcode, {{{{"x", V::I32}, {"y", V::I32}, {"color", V::Color}}, Opcode::Pixel}}}},
        {"rect", {V::Void, L::Opcode, {{{{"x", V::F32}, {"y", V::F32}, {"w", V::F32}, {"h", V::F32}, {"color", V::Color}}, Opcode::Rect}}}},
        {"line", {V::Void, L::Opcode, {{{{"ax", V::F32}, {"ay", V::F32}, {"bx", V::F32}, {"by", V::F32}, {"thickness", V::I32}, {"color", V::Color}}, Opcode::Line}}}},
        {"curve", {V::Void, L::Opcode, {
            {{{"a", V::Vec2}, {"b", V::Vec2}, {"c", V::Vec2}, {"thickness", V::F32}, {"color", V::Color}}, Opcode::Curve},
            {{{"a", V::Vec2}, {"b", V::Vec2}, {"c", V::Vec2}, {"d", V::Vec2}, {"thickness", V::F32}, {"color", V::Color}}, Opcode::CurveCubic}}}},
        {"oval", {V::Void, L::Opcode, {{{{"x", V::F32}, {"y", V::F32}, {"rx", V::F32}, {"ry", V::F32}, {"color", V::Color}}, Opcode::Oval}}}},
        {"weather", {V::Void, L::Opcode, {{{{"seed", V::I32}, {"amount", V::I32}}, Opcode::Weather}}}},
        {"poly", {V::Void, L::Polygon, {}}},
    };
    return table;
}

const BuiltinOverload* builtinOverload(const BuiltinSpec& spec, std::size_t arity) {
    for (const auto& overload : spec.overloads) {
        if (overload.params.size() == arity) return &overload;
    }
    return nullptr;
}

const BuiltinOverload* builtinOverload(const BuiltinSpec& spec,
                                       std::span<const ValueType> arguments) {
    const BuiltinOverload* best = nullptr;
    std::size_t bestWidenings = arguments.size() + 1;
    for (const auto& candidate : spec.overloads) {
        if (candidate.params.size() != arguments.size()) continue;
        std::size_t widenings = 0;
        bool fits = true;
        for (std::size_t i = 0; i < arguments.size(); ++i) {
            if (!argFits(candidate.params[i].type, arguments[i])) { fits = false; break; }
            if (candidate.params[i].type != arguments[i]) ++widenings;
        }
        if (fits && widenings < bestWidenings) {
            best = &candidate;
            bestWidenings = widenings;
        }
    }
    return best;
}

std::optional<ValueType> selectionType(std::span<const ValueType> arguments) {
    if (arguments.empty() || !isStorable(arguments[0])) return std::nullopt;
    auto result = arguments[0];
    for (const auto type : arguments) {
        if (isNumeric(result) && isNumeric(type)) result = promote(result, type);
        else if (type != result) return std::nullopt;
    }
    return result;
}
} // namespace iprop::ips
