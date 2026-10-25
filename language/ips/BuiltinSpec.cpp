#include "ips/BuiltinSpec.hpp"
#include "iprop/Samplers.hpp"
#include "iprop/ListOperations.hpp"
#include <stdexcept>

namespace iprop::ips {
namespace {
std::map<std::string, BuiltinSpec> makeBuiltinSpecs() {
    using V = ValueType;
    using L = BuiltinLowering;
    std::map<std::string, BuiltinSpec> table = {
        {"unit", {V::F32, L::Opcode, {{{}, Opcode::RngUnit}}}},
        {"stream_push", {V::Void, L::Opcode, {{{{"seed", V::I32}}, Opcode::StreamPush}}}},
        {"stream_pop", {V::Void, L::Opcode, {{{}, Opcode::StreamPop}}}},
        {"range", {V::F32, L::Opcode, {{{{"a", V::F32}, {"b", V::F32}}, Opcode::RngRange}}}},
        {"index", {V::I32, L::Opcode, {{{{"n", V::I32}}, Opcode::RngIndex}}}},
        {"pick", {V::I32, L::Pick, {{{{"n", V::I32}}, Opcode::RngIndex}}}},
        {"chance", {V::I32, L::Chance, {{{{"p", V::F32}}, std::nullopt}}}},
        {"roll", {V::F32, L::Roll, {{{{"seed", V::I32}, {"a", V::F32}, {"b", V::F32}}, std::nullopt}}}},
        {"choose", {L::Choose, {}}},
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
        {"min", {L::Opcode, {
            {{{"x", V::F32}, {"y", V::F32}}, Opcode::F32Min, V::F32},
            {{{"x", V::I32}, {"y", V::I32}}, Opcode::I32Min, V::I32}}}},
        {"max", {L::Opcode, {
            {{{"x", V::F32}, {"y", V::F32}}, Opcode::F32Max, V::F32},
            {{{"x", V::I32}, {"y", V::I32}}, Opcode::I32Max, V::I32}}}},
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
        {"vec2.normalize", {V::Vec2, L::Opcode, {{{{"v", V::Vec2}}, Opcode::Vec2Normalize}}}},
        {"vec2.dot", {V::F32, L::Opcode, {{{{"a", V::Vec2}, {"b", V::Vec2}}, Opcode::Vec2Dot}}}},
        {"vec2.rot", {V::Vec2, L::Opcode, {{{{"v", V::Vec2}, {"a", V::F32}}, Opcode::Vec2Rot}}}},
        {"smoothstep", {V::F32, L::Opcode, {{{{"a", V::F32}, {"b", V::F32}, {"t", V::F32}}, Opcode::Smoothstep}}}},
        {"remap", {V::F32, L::Opcode, {{{{"t", V::F32}, {"a", V::F32}, {"b", V::F32}, {"c", V::F32}, {"d", V::F32}}, Opcode::Remap}}}},
        {"clamp", {L::Opcode, {
            {{{"x", V::F32}, {"min", V::F32}, {"max", V::F32}}, Opcode::Clamp, V::F32},
            {{{"x", V::I32}, {"min", V::I32}, {"max", V::I32}}, Opcode::I32Clamp, V::I32}}}},
        {"i32trunc", {V::I32, L::Opcode, {{{{"x", V::F32}}, Opcode::F32ToI32}}}},
        {"int", {V::I32, L::Opcode, {
            {{{"x", V::I32}}, std::nullopt}, {{{"x", V::F32}}, Opcode::F32ToI32}}}},
        {"float", {V::F32, L::Opcode, {
            {{{"x", V::F32}}, std::nullopt}, {{{"x", V::I32}}, Opcode::I32ToF32}}}},
        {"rgb", {V::Color, L::ColorConstructor, {{{{"r", V::I32}, {"g", V::I32}, {"b", V::I32}}, std::nullopt}}}},
        {"rgba", {V::Color, L::ColorConstructor, {{{{"r", V::I32}, {"g", V::I32}, {"b", V::I32}, {"a", V::I32}}, std::nullopt}}}},
        {"round", {V::I32, L::Round, {{{{"x", V::F32}}, std::nullopt}}}},
        {"gaussian", {V::Distribution, L::Opcode,
            {{{{"mean", V::F32}, {"sigma", V::F32}}, Opcode::DistributionGaussian}}}},
        {"triangular", {V::Distribution, L::Opcode,
            {{{{"min", V::F32}, {"peak", V::F32}, {"max", V::F32}}, Opcode::DistributionTriangular}}}},
        // Without min and max the natural range is 0-1.
        {"beta", {V::Distribution, L::Beta, {
            {{{"alpha", V::F32}, {"beta", V::F32}}, std::nullopt},
            {{{"alpha", V::F32}, {"beta", V::F32}, {"min", V::F32}, {"max", V::F32}}, Opcode::DistributionBeta}}}},
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
        {"poly", {V::Void, L::Polygon, {
            {{{"points", V::list(V::Vec2)}, {"color", V::Color}}, Opcode::PolyList}}}},
        {"surface.paint", {V::Void, L::SurfacePaint, {
            {{{"image", V::Layer}}, Opcode::SurfacePaint},
            {{{"image", V::Layer}, {"blend", V::I32}}, Opcode::SurfacePaint},
            {{{"image", V::Mask}, {"color", V::Color}}, Opcode::SurfacePaintMask},
            {{{"image", V::Mask}, {"color", V::Color}, {"blend", V::I32}}, Opcode::SurfacePaintMask}}}},
        {"mask.from", {V::Mask, L::Opcode, {{{{"image", V::Layer}}, Opcode::MaskFrom}}}},
        {"mask.union", {V::Mask, L::Opcode, {{{{"a", V::Mask}, {"b", V::Mask}}, Opcode::MaskUnion}}}},
        {"mask.intersect", {V::Mask, L::Opcode, {{{{"a", V::Mask}, {"b", V::Mask}}, Opcode::MaskIntersect}}}},
        {"mask.subtract", {V::Mask, L::Opcode, {{{{"a", V::Mask}, {"b", V::Mask}}, Opcode::MaskSubtract}}}},
        {"mask.invert", {V::Mask, L::Opcode, {{{{"source", V::Mask}}, Opcode::MaskInvert}}}},
        {"mask.expand", {V::Mask, L::Opcode, {{{{"source", V::Mask}, {"radius", V::I32}}, Opcode::MaskExpand}}}},
        {"mask.shrink", {V::Mask, L::Opcode, {{{{"source", V::Mask}, {"radius", V::I32}}, Opcode::MaskShrink}}}},
        {"mask.offset", {V::Mask, L::Opcode, {{{{"source", V::Mask}, {"dx", V::I32}, {"dy", V::I32}}, Opcode::MaskOffset}}}},
        {"list.len", {V::I32, L::ListLength, {
            {{{"items", V::list(V::I32)}}, Opcode::ListLen},
            {{{"items", V::list(V::F32)}}, Opcode::ListLen},
            {{{"items", V::list(V::Color)}}, Opcode::ListLen},
            {{{"items", V::list(V::Vec2)}}, Opcode::ListLen},
            {{{"items", V::list(V::Distribution)}}, Opcode::ListLen},
            {{{"items", V::list(V::sampler(V::I32))}}, Opcode::ListLen},
            {{{"items", V::list(V::sampler(V::F32))}}, Opcode::ListLen},
            {{{"items", V::list(V::sampler(V::Color))}}, Opcode::ListLen},
            {{{"items", V::list(V::sampler(V::Vec2))}}, Opcode::ListLen},
            {{{"items", V::list(V::stepper(V::I32))}}, Opcode::ListLen},
            {{{"items", V::list(V::stepper(V::F32))}}, Opcode::ListLen},
            {{{"items", V::list(V::stepper(V::Color))}}, Opcode::ListLen},
            {{{"items", V::list(V::stepper(V::Vec2))}}, Opcode::ListLen}}}},
    };
    const auto add = [&](const std::string& name, std::vector<BuiltinParameter> params,
                         BuiltinAdaptation adaptation = BuiltinAdaptation::Coordinates) {
        auto& spec = table.at(name);
        const auto& scalar = spec.overloads.front();
        spec.overloads.push_back({std::move(params), scalar.opcode,
            adaptation == BuiltinAdaptation::VectorLerp ? V::Vec2 : scalar.result, adaptation});
    };
    for (const auto* name : {"noise", "fault", "noise.grad", "noise.cell"})
        add(name, {{"p", V::Vec2}, {"seed", V::I32}});
    for (const auto* name : {"noise.fbm", "noise.ridge"})
        add(name, {{"p", V::Vec2}, {"octaves", V::I32}, {"seed", V::I32}});
    add("warp", {{"p", V::Vec2}, {"amount", V::F32}, {"seed", V::I32}});
    add("rect", {{"position", V::Vec2}, {"size", V::Vec2}, {"color", V::Color}});
    add("rect", {{"position", V::Vec2}, {"w", V::F32}, {"h", V::F32}, {"color", V::Color}});
    add("rect", {{"x", V::F32}, {"y", V::F32}, {"size", V::Vec2}, {"color", V::Color}});
    add("line", {{"a", V::Vec2}, {"b", V::Vec2}, {"thickness", V::I32}, {"color", V::Color}});
    add("oval", {{"center", V::Vec2}, {"radii", V::Vec2}, {"color", V::Color}});
    add("oval", {{"center", V::Vec2}, {"rx", V::F32}, {"ry", V::F32}, {"color", V::Color}});
    add("oval", {{"x", V::F32}, {"y", V::F32}, {"radii", V::Vec2}, {"color", V::Color}});
    add("pixel", {{"p", V::Vec2}, {"color", V::Color}}, BuiltinAdaptation::IntegerCoordinates);
    add("cell.jitter", {{"cell", V::Vec2}, {"seed", V::I32}}, BuiltinAdaptation::IntegerCoordinates);
    add("dither", {{"t", V::F32}, {"p", V::Vec2}}, BuiltinAdaptation::IntegerCoordinates);
    add("mask.offset", {{"source", V::Mask}, {"offset", V::Vec2}}, BuiltinAdaptation::IntegerCoordinates);
    add("atan2", {{"v", V::Vec2}}, BuiltinAdaptation::VectorAngle);
    add("lerp", {{"a", V::Vec2}, {"b", V::Vec2}, {"t", V::F32}}, BuiltinAdaptation::VectorLerp);
    return table;
}
}

const std::map<std::string, BuiltinSpec>& builtinSpecs() {
    static const auto table = makeBuiltinSpecs();
    return table;
}

std::vector<BuiltinStep> builtinExpansion(const BuiltinOverload& overload) {
    std::vector<BuiltinStep> steps;
    // Construct the variant before insertion so GCC 13 can track its lifetime
    // across vector growth without a false stringop-overflow diagnostic.
    const auto component = [&](std::size_t argument, Opcode projection) {
        steps.push_back(argument);
        steps.push_back(projection);
        if (overload.adaptation == BuiltinAdaptation::IntegerCoordinates)
            steps.push_back(Opcode::F32ToI32);
    };
    if (overload.adaptation == BuiltinAdaptation::VectorLerp) {
        for (const auto projection : {Opcode::Vec2X, Opcode::Vec2Y}) {
            component(0, projection);
            component(1, projection);
            steps.push_back(std::size_t{2});
            steps.push_back(Opcode::Lerp);
        }
        steps.push_back(Opcode::Vec2Make);
    } else if (overload.adaptation == BuiltinAdaptation::VectorAngle) {
        component(0, Opcode::Vec2Y);
        component(0, Opcode::Vec2X);
        steps.push_back(*overload.opcode);
    } else {
        for (std::size_t i = 0; i < overload.params.size(); ++i) {
            if (overload.adaptation != BuiltinAdaptation::Direct && overload.params[i].type == ValueType::Vec2) {
                component(i, Opcode::Vec2X);
                component(i, Opcode::Vec2Y);
            } else steps.push_back(i);
        }
        if (overload.opcode) steps.push_back(*overload.opcode);
    }
    return steps;
}

const std::map<std::string, BuiltinSpec>& surfaceMethodSpecs() {
    using V = ValueType;
    static const std::map<std::string, BuiltinSpec> methods{
        {"blur", {BuiltinLowering::Opcode, {
            {{{"receiver", V::Layer}, {"sigma", V::F32}}, Opcode::LayerBlur, V::Layer},
            {{{"receiver", V::Mask}, {"sigma", V::F32}}, Opcode::MaskBlur, V::Mask}}}}
    };
    return methods;
}

const std::map<std::string, BuiltinSpec>& listMethodSpecs() {
    static const auto specs = [] {
        std::vector<BuiltinOverload> distinct, sort;
        for (const auto item : kListElementTypes) {
            const auto type = ValueType::list(item);
            distinct.push_back({{{"receiver", type}}, Opcode::ListDistinct, type});
            if (isSortableList(type)) sort.push_back({{{"receiver", type}}, Opcode::ListSort, type});
        }
        return std::map<std::string, BuiltinSpec>{
            {"distinct", {BuiltinLowering::Opcode, std::move(distinct)}},
            {"sort", {BuiltinLowering::Opcode, std::move(sort)}}};
    }();
    return specs;
}

const BuiltinSpec& samplerPickSpec() {
    static const BuiltinSpec spec = [] {
        std::vector<BuiltinOverload> overloads;
        for (const auto item : kSamplerElementTypes) {
            const auto receiver = ValueType::sampler(item);
            overloads.push_back({{{"receiver", receiver}}, Opcode::SamplerPick, item});
            overloads.push_back({{{"receiver", receiver}, {"n", ValueType::I32}}, Opcode::SamplerPickList, ValueType::list(item)});
        }
        return BuiltinSpec(BuiltinLowering::Opcode, std::move(overloads));
    }();
    return spec;
}

const BuiltinSpec& stepperPickSpec() {
    static const BuiltinSpec spec = [] {
        std::vector<BuiltinOverload> overloads;
        for (const auto item : kSamplerElementTypes)
            overloads.push_back({{{"receiver", ValueType::stepper(item)}, {"n", ValueType::I32}},
                Opcode::StepperPickList, ValueType::list(item)});
        return BuiltinSpec(BuiltinLowering::Opcode, std::move(overloads));
    }();
    return spec;
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

bool variadicPolygon(const BuiltinSpec& spec, std::size_t arity) {
    return spec.lowering == BuiltinLowering::Polygon && !builtinOverload(spec, arity);
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

bool builtinYieldsToHelper(std::string_view name) {
    return name == "gaussian" || name == "triangular" || name == "beta";
}

OperationEffects builtinEffects(const BuiltinSpec& spec, const BuiltinOverload* overload) {
    if (overload && overload->opcode) {
        const auto* opcode = findOpcode(*overload->opcode);
        if (!opcode) throw std::logic_error("builtin effects refer to an unknown opcode");
        return opcode->effects;
    }
    switch (spec.lowering) {
    case BuiltinLowering::Pick: case BuiltinLowering::Chance:
        return {RngEffect::AdvanceCurrent};
    case BuiltinLowering::Roll: case BuiltinLowering::Choose:
        return {RngEffect::IsolatedSeededStream};
    case BuiltinLowering::Polygon: return findOpcode(Opcode::Poly)->effects;
    case BuiltinLowering::Opcode:
        if (!overload) throw std::logic_error("fixed builtin effects require an overload");
        return {};
    case BuiltinLowering::ListLength: return findOpcode(Opcode::ListLen)->effects;
    case BuiltinLowering::SurfacePaint: return findOpcode(Opcode::SurfacePaint)->effects;
    case BuiltinLowering::Round:
    case BuiltinLowering::ColorConstructor: return {};
    case BuiltinLowering::Beta: return findOpcode(Opcode::DistributionBeta)->effects;
    }
    throw std::logic_error("unknown builtin lowering effects");
}
} // namespace iprop::ips
