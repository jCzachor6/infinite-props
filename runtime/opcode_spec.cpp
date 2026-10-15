#include "iprop/OpcodeSpec.hpp"
#include <map>

namespace iprop {
namespace {
using S = StackType;
using V = ValueType;
constexpr OperationEffects effects(Opcode op) {
    using R = RngEffect;
    using C = EffectCheck;
    const auto check = [](C value) { return static_cast<u16>(value); };
    switch (op) {
    case Opcode::RngUnit: case Opcode::RngRange: case Opcode::RngIndex:
        return {R::AdvanceCurrent};
    case Opcode::RngInt: case Opcode::RngFloat: case Opcode::RngIntWith: case Opcode::RngFloatWith:
        return {R::AdvanceCurrent,false,false,check(C::NumericRange)};
    case Opcode::DistributionGaussian: case Opcode::DistributionTriangular: case Opcode::DistributionBeta:
        return {R::None,false,false,check(C::NumericRange)};
    case Opcode::StreamState: return {R::ReadCurrent};
    case Opcode::StreamPush: return {R::PushSeededStream};
    case Opcode::StreamPop: return {R::RestoreStream,false,false,check(C::StreamStackUnderflow)};
    case Opcode::Noise: case Opcode::Fault: return {R::SeededComputation};
    case Opcode::NoiseGrad: case Opcode::NoiseFbm: case Opcode::NoiseCell:
    case Opcode::Warp: case Opcode::CellJitter: case Opcode::NoiseRidge:
        return {R::SeededComputation,false,false,check(C::NumericRange)};
    case Opcode::Weather: return {R::SeededComputation,true};
    case Opcode::Pixel: case Opcode::Rect: case Opcode::Line: case Opcode::Poly:
    case Opcode::Oval: case Opcode::Curve: case Opcode::CurveCubic:
        return {R::None,true,false,check(C::NumericRange)};
    case Opcode::ListBegin: return {R::None,false,false,check(C::NumericRange)};
    // A dynamic count can leave a builder over- or under-filled.
    case Opcode::ListAppend: case Opcode::ListEnd: return {R::None,false,false,check(C::IndexRange)};
    case Opcode::DistributionBegin: return {R::None,false,false,check(C::NumericRange)};
    case Opcode::DistributionAppend: case Opcode::DistributionEnd:
        return {R::None,false,false,static_cast<u16>(check(C::NumericRange)|check(C::IndexRange))};
    case Opcode::SamplerValue: case Opcode::SamplerRange: case Opcode::SamplerWith:
    case Opcode::StepperValue: case Opcode::StepperRange: case Opcode::StepperWith:
        return {R::None,false,false,check(C::NumericRange)};
    case Opcode::StepperPickList:
        return {R::None,false,false,static_cast<u16>(check(C::NumericRange)|check(C::UnassignedHandle))};
    case Opcode::SamplerPick: case Opcode::SamplerPickList:
        return {R::AdvanceCurrent,false,false,static_cast<u16>(check(C::NumericRange)|check(C::UnassignedHandle))};
    case Opcode::ListGet: return {R::None,false,false,static_cast<u16>(check(C::IndexRange)|check(C::UnassignedHandle))};
    case Opcode::ListLen: case Opcode::ListDistinct: case Opcode::ListSort:
        return {R::None,false,false,check(C::UnassignedHandle)};
    // Scopes are limited across the whole call stack, not only lexically.
    case Opcode::LayerBegin: return {R::None,true,false,check(C::ScopeDepth)};
    case Opcode::ClipBegin:
        return {R::None,false,false,static_cast<u16>(check(C::UnassignedHandle)|check(C::NumericRange)|check(C::ScopeDepth))};
    case Opcode::SurfacePaint: return {R::None,true,false,check(C::UnassignedHandle)};
    // Mask views check their mapping and raster scale against the target.
    case Opcode::SurfacePaintMask:
        return {R::None,true,false,static_cast<u16>(check(C::UnassignedHandle)|check(C::NumericRange))};
    case Opcode::MaskFrom: case Opcode::MaskInvert:
    case Opcode::MaskUnion: case Opcode::MaskIntersect: case Opcode::MaskSubtract:
    case Opcode::MaskExpand: case Opcode::MaskShrink: case Opcode::MaskOffset:
    case Opcode::LayerBlur: case Opcode::MaskBlur:
        return {R::None,false,false,static_cast<u16>(check(C::UnassignedHandle)|check(C::NumericRange))};
    case Opcode::PolyList: return {R::None,true,false,static_cast<u16>(check(C::NumericRange)|check(C::UnassignedHandle))};
    case Opcode::ColorHsv: case Opcode::Vec2Mix: case Opcode::Vec2Normalize:
        return {R::None,false,false,check(C::NumericRange)};
    case Opcode::OutPoint: case Opcode::OutRect: case Opcode::OutCircle:
    case Opcode::OutListPoint: case Opcode::OutListRect: case Opcode::OutListCircle:
        return {R::None,false,true};
    case Opcode::OutMask: case Opcode::OutLayer:
        return {R::None,false,true,static_cast<u16>(check(C::UnassignedHandle)|check(C::PartOutputContext)|check(C::NumericRange))};
    case Opcode::PartGetMask: case Opcode::PartGetLayer:
        return {R::None,false,false,static_cast<u16>(check(C::PartOutputMissing)|check(C::PartOutputContext))};
    case Opcode::PartGetPoint: case Opcode::PartGetRect: case Opcode::PartGetCircle:
        return {R::None,false,false,static_cast<u8>(check(C::PartOutputMissing)|check(C::PartOutputContext))};
    case Opcode::I32DivS:
        return {R::None,false,false,static_cast<u8>(check(C::DivideByZero)|check(C::IntegerOverflow))};
    case Opcode::I32DivU: case Opcode::I32RemS: case Opcode::I32RemU:
        return {R::None,false,false,check(C::DivideByZero)};
    case Opcode::Call: return {R::SharedCallee,true,true,check(C::Callee),true};
    case Opcode::PartDraw:
        return {R::IsolatedCallee,true,true,
            static_cast<u8>(check(C::PartRenderArguments)|check(C::Callee)),true};
    default: return {};
    }
}
constexpr OpcodeSpec fixed(Opcode op, const char* name, StackSignature stack) {
    return {op,name,OperandKind::None,AuxPolicy::Zero,stack,effects(op)};
}
constexpr OpcodeSpec dynamic(Opcode op, const char* name, OperandKind operand,
                             AuxPolicy aux = AuxPolicy::Zero) {
    return {op,name,operand,aux,std::nullopt,effects(op)};
}

constexpr OpcodeSpec kSpecs[] = {
    fixed(Opcode::Nop, "nop", StackSignature({})),
    dynamic(Opcode::Block, "block", OperandKind::None),
    dynamic(Opcode::Loop, "loop", OperandKind::None),
    dynamic(Opcode::If, "if", OperandKind::None),
    dynamic(Opcode::Else, "else", OperandKind::None),
    dynamic(Opcode::End, "end", OperandKind::None),
    dynamic(Opcode::Br, "br", OperandKind::Depth),
    dynamic(Opcode::BrIf, "br_if", OperandKind::Depth),
    dynamic(Opcode::Call, "call", OperandKind::Name),
    dynamic(Opcode::Return, "return", OperandKind::None),
    dynamic(Opcode::ConstI32, "const.i32", OperandKind::IntConst),
    dynamic(Opcode::ConstF32, "const.f32", OperandKind::FloatConst),
    dynamic(Opcode::ConstColor, "const.color", OperandKind::ColorConst),
    dynamic(Opcode::LocalGet, "local.get", OperandKind::Local),
    dynamic(Opcode::LocalSet, "local.set", OperandKind::Local),
    dynamic(Opcode::LocalTee, "local.tee", OperandKind::Local),
    dynamic(Opcode::Drop, "drop", OperandKind::None),
    dynamic(Opcode::Dup, "dup", OperandKind::None),
    fixed(Opcode::I32Add, "i32.add", StackSignature({S::I32, S::I32}, V::I32)),
    fixed(Opcode::I32Sub, "i32.sub", StackSignature({S::I32, S::I32}, V::I32)),
    fixed(Opcode::I32Mul, "i32.mul", StackSignature({S::I32, S::I32}, V::I32)),
    fixed(Opcode::I32DivS, "i32.div_s", StackSignature({S::I32, S::I32}, V::I32)),
    fixed(Opcode::I32DivU, "i32.div_u", StackSignature({S::I32, S::I32}, V::I32)),
    fixed(Opcode::I32RemS, "i32.rem_s", StackSignature({S::I32, S::I32}, V::I32)),
    fixed(Opcode::I32RemU, "i32.rem_u", StackSignature({S::I32, S::I32}, V::I32)),
    fixed(Opcode::I32And, "i32.and", StackSignature({S::I32, S::I32}, V::I32)),
    fixed(Opcode::I32Or, "i32.or", StackSignature({S::I32, S::I32}, V::I32)),
    fixed(Opcode::I32Xor, "i32.xor", StackSignature({S::I32, S::I32}, V::I32)),
    fixed(Opcode::I32Shl, "i32.shl", StackSignature({S::I32, S::I32}, V::I32)),
    fixed(Opcode::I32ShrS, "i32.shr_s", StackSignature({S::I32, S::I32}, V::I32)),
    fixed(Opcode::I32ShrU, "i32.shr_u", StackSignature({S::I32, S::I32}, V::I32)),
    fixed(Opcode::I32Neg, "i32.neg", StackSignature({S::I32}, V::I32)),
    fixed(Opcode::I32Not, "i32.not", StackSignature({S::I32}, V::I32)),
    fixed(Opcode::I32Eq, "i32.eq", StackSignature({S::I32, S::I32}, V::I32)),
    fixed(Opcode::I32Ne, "i32.ne", StackSignature({S::I32, S::I32}, V::I32)),
    fixed(Opcode::I32LtS, "i32.lt_s", StackSignature({S::I32, S::I32}, V::I32)),
    fixed(Opcode::I32LtU, "i32.lt_u", StackSignature({S::I32, S::I32}, V::I32)),
    fixed(Opcode::I32GtS, "i32.gt_s", StackSignature({S::I32, S::I32}, V::I32)),
    fixed(Opcode::I32GtU, "i32.gt_u", StackSignature({S::I32, S::I32}, V::I32)),
    fixed(Opcode::I32LeS, "i32.le_s", StackSignature({S::I32, S::I32}, V::I32)),
    fixed(Opcode::I32LeU, "i32.le_u", StackSignature({S::I32, S::I32}, V::I32)),
    fixed(Opcode::I32GeS, "i32.ge_s", StackSignature({S::I32, S::I32}, V::I32)),
    fixed(Opcode::I32GeU, "i32.ge_u", StackSignature({S::I32, S::I32}, V::I32)),
    fixed(Opcode::F32Add, "f32.add", StackSignature({S::F32, S::F32}, V::F32)),
    fixed(Opcode::F32Sub, "f32.sub", StackSignature({S::F32, S::F32}, V::F32)),
    fixed(Opcode::F32Mul, "f32.mul", StackSignature({S::F32, S::F32}, V::F32)),
    fixed(Opcode::F32Div, "f32.div", StackSignature({S::F32, S::F32}, V::F32)),
    fixed(Opcode::F32Eq, "f32.eq", StackSignature({S::F32, S::F32}, V::I32)),
    fixed(Opcode::F32Ne, "f32.ne", StackSignature({S::F32, S::F32}, V::I32)),
    fixed(Opcode::F32Lt, "f32.lt", StackSignature({S::F32, S::F32}, V::I32)),
    fixed(Opcode::F32Gt, "f32.gt", StackSignature({S::F32, S::F32}, V::I32)),
    fixed(Opcode::F32Le, "f32.le", StackSignature({S::F32, S::F32}, V::I32)),
    fixed(Opcode::F32Ge, "f32.ge", StackSignature({S::F32, S::F32}, V::I32)),
    fixed(Opcode::F32Neg, "f32.neg", StackSignature({S::F32}, V::F32)),
    fixed(Opcode::F32Abs, "f32.abs", StackSignature({S::F32}, V::F32)),
    fixed(Opcode::F32Sqrt, "f32.sqrt", StackSignature({S::F32}, V::F32)),
    fixed(Opcode::I32ToF32, "i32.to.f32", StackSignature({S::I32}, V::F32)),
    fixed(Opcode::F32ToI32, "f32.to.i32", StackSignature({S::F32}, V::I32)),
    fixed(Opcode::F32Sin, "f32.sin", StackSignature({S::F32}, V::F32)),
    fixed(Opcode::ColorMix, "color.mix", StackSignature({S::Color, S::Color, S::F32}, V::Color)),
    fixed(Opcode::ColorShade, "color.shade", StackSignature({S::Color, S::I32}, V::Color)),
    fixed(Opcode::ColorSetA, "color.set.a", StackSignature({S::Color, S::I32}, V::Color)),
    fixed(Opcode::F32Cos, "f32.cos", StackSignature({S::F32}, V::F32)),
    fixed(Opcode::F32Floor, "f32.floor", StackSignature({S::F32}, V::F32)),
    fixed(Opcode::F32Ceil, "f32.ceil", StackSignature({S::F32}, V::F32)),
    fixed(Opcode::F32Min, "f32.min", StackSignature({S::F32, S::F32}, V::F32)),
    fixed(Opcode::F32Max, "f32.max", StackSignature({S::F32, S::F32}, V::F32)),
    fixed(Opcode::F32Pow, "f32.pow", StackSignature({S::F32, S::F32}, V::F32)),
    fixed(Opcode::F32Exp, "f32.exp", StackSignature({S::F32}, V::F32)),
    fixed(Opcode::F32Atan2, "f32.atan2", StackSignature({S::F32, S::F32}, V::F32)),
    fixed(Opcode::Vec2Make, "vec2.make", StackSignature({S::F32, S::F32}, V::Vec2)),
    fixed(Opcode::Vec2Add, "vec2.add", StackSignature({S::Vec2, S::Vec2}, V::Vec2)),
    fixed(Opcode::Vec2Sub, "vec2.sub", StackSignature({S::Vec2, S::Vec2}, V::Vec2)),
    fixed(Opcode::Vec2Mul, "vec2.mul", StackSignature({S::Vec2, S::ScalarOrVec2}, V::Vec2)),
    fixed(Opcode::Vec2Div, "vec2.div", StackSignature({S::Vec2, S::ScalarOrVec2}, V::Vec2)),
    fixed(Opcode::StreamPush, "stream.push", StackSignature({S::I32})),
    fixed(Opcode::StreamPop, "stream.pop", StackSignature({})),
    fixed(Opcode::RngUnit, "rng.unit", StackSignature({}, V::F32)),
    fixed(Opcode::RngRange, "rng.range", StackSignature({S::F32, S::F32}, V::F32)),
    fixed(Opcode::RngIndex, "rng.index", StackSignature({S::I32}, V::I32)),
    fixed(Opcode::Hash, "hash", StackSignature({S::I32}, V::I32)),
    fixed(Opcode::StreamState, "stream.state", StackSignature({}, V::I32)),
    fixed(Opcode::Noise, "noise", StackSignature({S::F32, S::F32, S::I32}, V::F32)),
    fixed(Opcode::Fault, "fault", StackSignature({S::F32, S::F32, S::I32}, V::F32)),
    fixed(Opcode::Smooth, "smooth", StackSignature({S::F32}, V::F32)),
    fixed(Opcode::Lerp, "lerp", StackSignature({S::F32, S::F32, S::F32}, V::F32)),
    fixed(Opcode::Vec2Neg, "vec2.neg", StackSignature({S::Vec2}, V::Vec2)),
    fixed(Opcode::Vec2Len, "vec2.len", StackSignature({S::Vec2}, V::F32)),
    fixed(Opcode::Vec2Dot, "vec2.dot", StackSignature({S::Vec2, S::Vec2}, V::F32)),
    fixed(Opcode::Vec2Rot, "vec2.rot", StackSignature({S::Vec2, S::F32}, V::Vec2)),
    fixed(Opcode::Vec2X, "vec2.x", StackSignature({S::Vec2}, V::F32)),
    fixed(Opcode::Vec2Y, "vec2.y", StackSignature({S::Vec2}, V::F32)),
    fixed(Opcode::DistCircle, "dist.circle", StackSignature({S::Vec2, S::Vec2, S::F32}, V::F32)),
    fixed(Opcode::DistRect, "dist.rect", StackSignature({S::Vec2, S::Vec2, S::Vec2}, V::F32)),
    fixed(Opcode::DistRoundRect, "dist.roundrect", StackSignature({S::Vec2, S::Vec2, S::Vec2, S::F32}, V::F32)),
    fixed(Opcode::DistLine, "dist.line", StackSignature({S::Vec2, S::Vec2, S::Vec2}, V::F32)),
    fixed(Opcode::DistBezier, "dist.bezier", StackSignature({S::Vec2, S::Vec2, S::Vec2, S::Vec2}, V::F32)),
    fixed(Opcode::Fill, "fill", StackSignature({S::F32, S::F32}, V::F32)),
    fixed(Opcode::Pixel, "pixel", StackSignature({S::I32, S::I32, S::Color})),
    fixed(Opcode::Rect, "rect", StackSignature({S::F32, S::F32, S::F32, S::F32, S::Color})),
    fixed(Opcode::Line, "line", StackSignature({S::F32, S::F32, S::F32, S::F32, S::I32, S::Color})),
    dynamic(Opcode::Poly, "poly", OperandKind::Count),
    fixed(Opcode::Oval, "oval", StackSignature({S::F32, S::F32, S::F32, S::F32, S::Color})),
    fixed(Opcode::Weather, "weather", StackSignature({S::I32, S::I32})),
    fixed(Opcode::Curve, "curve", StackSignature({S::Vec2, S::Vec2, S::Vec2, S::F32, S::Color})),
    fixed(Opcode::CurveCubic, "curve.cubic", StackSignature({S::Vec2, S::Vec2, S::Vec2, S::Vec2, S::F32, S::Color})),
    fixed(Opcode::DistBezierCubic, "dist.bezier.cubic", StackSignature({S::Vec2, S::Vec2, S::Vec2, S::Vec2, S::Vec2}, V::F32)),
    fixed(Opcode::NoiseGrad, "noise.grad", StackSignature({S::F32, S::F32, S::I32}, V::F32)),
    fixed(Opcode::NoiseFbm, "noise.fbm", StackSignature({S::F32, S::F32, S::I32, S::I32}, V::F32)),
    fixed(Opcode::NoiseCell, "noise.cell", StackSignature({S::F32, S::F32, S::I32}, V::F32)),
    fixed(Opcode::Warp, "warp", StackSignature({S::F32, S::F32, S::F32, S::I32}, V::F32)),
    fixed(Opcode::CellJitter, "cell.jitter", StackSignature({S::I32, S::I32, S::I32}, V::Vec2)),
    fixed(Opcode::NoiseRidge, "noise.ridge", StackSignature({S::F32, S::F32, S::I32, S::I32}, V::F32)),
    fixed(Opcode::Gain, "gain", StackSignature({S::F32, S::F32}, V::F32)),
    dynamic(Opcode::OutPoint, "out.point", OperandKind::Name),
    dynamic(Opcode::OutRect, "out.rect", OperandKind::Name),
    dynamic(Opcode::OutCircle, "out.circle", OperandKind::Name),
    dynamic(Opcode::OutListPoint, "out.list.point", OperandKind::Name),
    dynamic(Opcode::OutListRect, "out.list.rect", OperandKind::Name),
    dynamic(Opcode::OutListCircle, "out.list.circle", OperandKind::Name),
    dynamic(Opcode::PartGetPoint, "part.get.point", OperandKind::Name),
    dynamic(Opcode::PartGetRect, "part.get.rect", OperandKind::Name),
    dynamic(Opcode::PartGetCircle, "part.get.circle", OperandKind::Name),
    fixed(Opcode::Quintic, "quintic", StackSignature({S::F32}, V::F32)),
    fixed(Opcode::ColorMul, "color.mul", StackSignature({S::Color, S::Color}, V::Color)),
    fixed(Opcode::ColorScale, "color.scale", StackSignature({S::Color, S::F32}, V::Color)),
    fixed(Opcode::ColorHsv, "color.hsv", StackSignature({S::Color, S::F32, S::F32, S::F32}, V::Color)),
    fixed(Opcode::Dither, "dither", StackSignature({S::F32, S::I32, S::I32}, V::F32)),
    fixed(Opcode::Vec2Fold, "vec2.fold", StackSignature({S::Vec2, S::I32}, V::Vec2)),
    fixed(Opcode::Vec2Polar, "vec2.polar", StackSignature({S::Vec2}, V::Vec2)),
    dynamic(Opcode::PartDraw, "part.draw", OperandKind::Name, AuxPolicy::PartTransform),
    fixed(Opcode::Smoothstep, "smoothstep", StackSignature({S::F32, S::F32, S::F32}, V::F32)),
    fixed(Opcode::Remap, "remap", StackSignature({S::F32, S::F32, S::F32, S::F32, S::F32}, V::F32)),
    fixed(Opcode::Clamp, "clamp", StackSignature({S::F32, S::F32, S::F32}, V::F32)),
    fixed(Opcode::ColorR, "color.r", StackSignature({S::Color}, V::I32)),
    fixed(Opcode::ColorG, "color.g", StackSignature({S::Color}, V::I32)),
    fixed(Opcode::ColorB, "color.b", StackSignature({S::Color}, V::I32)),
    fixed(Opcode::ColorA, "color.a", StackSignature({S::Color}, V::I32)),
    fixed(Opcode::RngInt, "rng.int", StackSignature({S::I32, S::I32}, V::I32)),
    fixed(Opcode::RngFloat, "rng.float", StackSignature({S::F32, S::F32}, V::F32)),
    fixed(Opcode::DistributionGaussian, "distribution.gaussian",
          StackSignature({S::F32, S::F32}, V::Distribution)),
    fixed(Opcode::DistributionTriangular, "distribution.triangular",
          StackSignature({S::F32, S::F32, S::F32}, V::Distribution)),
    fixed(Opcode::DistributionBeta, "distribution.beta",
          StackSignature({S::F32, S::F32, S::F32, S::F32}, V::Distribution)),
    fixed(Opcode::RngIntWith, "rng.int.with", StackSignature({S::I32, S::I32, S::Distribution}, V::I32)),
    fixed(Opcode::RngFloatWith, "rng.float.with", StackSignature({S::F32, S::F32, S::Distribution}, V::F32)),
    fixed(Opcode::ColorMixRgba, "color.mix.rgba", StackSignature({S::Color, S::Color, S::F32}, V::Color)),
    fixed(Opcode::Vec2Mix, "vec2.mix", StackSignature({S::Vec2, S::Vec2, S::F32}, V::Vec2)),
    fixed(Opcode::I32Min, "i32.min", StackSignature({S::I32, S::I32}, V::I32)),
    fixed(Opcode::I32Max, "i32.max", StackSignature({S::I32, S::I32}, V::I32)),
    fixed(Opcode::I32Clamp, "i32.clamp", StackSignature({S::I32, S::I32, S::I32}, V::I32)),
    fixed(Opcode::Vec2Normalize, "vec2.normalize", StackSignature({S::Vec2}, V::Vec2)),
    dynamic(Opcode::ConstList, "const.list", OperandKind::ListConst),
    dynamic(Opcode::ListBegin, "list.begin", OperandKind::TypeDescriptor),
    dynamic(Opcode::ListAppend, "list.append", OperandKind::TypeDescriptor),
    dynamic(Opcode::ListEnd, "list.end", OperandKind::TypeDescriptor),
    dynamic(Opcode::ListGet, "list.get", OperandKind::TypeDescriptor),
    dynamic(Opcode::ListLen, "list.len", OperandKind::TypeDescriptor),
    dynamic(Opcode::PolyList, "poly.list", OperandKind::Zero),
    dynamic(Opcode::SamplerValue, "sampler.value", OperandKind::TypeDescriptor),
    dynamic(Opcode::SamplerRange, "sampler.range", OperandKind::TypeDescriptor),
    dynamic(Opcode::SamplerWith, "sampler.with", OperandKind::TypeDescriptor),
    dynamic(Opcode::SamplerPick, "sampler.pick", OperandKind::TypeDescriptor),
    dynamic(Opcode::SamplerPickList, "sampler.pick.list", OperandKind::TypeDescriptor),
    dynamic(Opcode::ConstSampler, "const.sampler", OperandKind::SamplerConst),
    dynamic(Opcode::ConstDistribution, "const.distribution", OperandKind::DistributionConst),
    dynamic(Opcode::DistributionBegin, "distribution.begin", OperandKind::Immediate),
    dynamic(Opcode::DistributionAppend, "distribution.append", OperandKind::Immediate),
    dynamic(Opcode::DistributionEnd, "distribution.end", OperandKind::Zero),
    dynamic(Opcode::LayerBegin, "layer.begin", OperandKind::Local, AuxPolicy::SurfaceMode),
    dynamic(Opcode::SurfacePaint, "surface.paint", OperandKind::Immediate),
    dynamic(Opcode::MaskFrom, "mask.from", OperandKind::Zero),
    dynamic(Opcode::MaskUnion, "mask.union", OperandKind::Zero),
    dynamic(Opcode::MaskIntersect, "mask.intersect", OperandKind::Zero),
    dynamic(Opcode::MaskSubtract, "mask.subtract", OperandKind::Zero),
    dynamic(Opcode::MaskInvert, "mask.invert", OperandKind::Zero),
    dynamic(Opcode::MaskExpand, "mask.expand", OperandKind::Zero),
    dynamic(Opcode::MaskShrink, "mask.shrink", OperandKind::Zero),
    dynamic(Opcode::MaskOffset, "mask.offset", OperandKind::Zero),
    dynamic(Opcode::SurfacePaintMask, "surface.paint.mask", OperandKind::Immediate),
    dynamic(Opcode::ClipBegin, "clip.begin", OperandKind::Zero),
    dynamic(Opcode::OutMask, "out.mask", OperandKind::ResourceExport),
    dynamic(Opcode::OutLayer, "out.layer", OperandKind::ResourceExport),
    dynamic(Opcode::PartGetMask, "part.get.mask", OperandKind::ResourceExport),
    dynamic(Opcode::PartGetLayer, "part.get.layer", OperandKind::ResourceExport),
    dynamic(Opcode::ListDistinct, "list.distinct", OperandKind::TypeDescriptor),
    dynamic(Opcode::ListSort, "list.sort", OperandKind::TypeDescriptor),
    dynamic(Opcode::StepperValue, "stepper.value", OperandKind::TypeDescriptor),
    dynamic(Opcode::StepperRange, "stepper.range", OperandKind::TypeDescriptor),
    dynamic(Opcode::StepperWith, "stepper.with", OperandKind::TypeDescriptor),
    dynamic(Opcode::StepperPickList, "stepper.pick.list", OperandKind::TypeDescriptor),
    dynamic(Opcode::ConstStepper, "const.stepper", OperandKind::StepperConst),
    dynamic(Opcode::LayerBlur, "layer.blur", OperandKind::Zero),
    dynamic(Opcode::MaskBlur, "mask.blur", OperandKind::Zero),
};
constexpr auto kByOpcode = [] {
    std::array<const OpcodeSpec*,256> index{};
    for (const auto& spec : kSpecs) index[static_cast<u8>(spec.opcode)] = &spec;
    return index;
}();
} // namespace

std::span<const OpcodeSpec> opcodeSpecs() { return kSpecs; }
const char* rngEffectName(RngEffect effect) {
    switch (effect) {
    case RngEffect::None: return "none";
    case RngEffect::ReadCurrent: return "read_current";
    case RngEffect::AdvanceCurrent: return "advance_current";
    case RngEffect::PushSeededStream: return "push_seeded_stream";
    case RngEffect::RestoreStream: return "restore_stream";
    case RngEffect::SeededComputation: return "seeded_computation";
    case RngEffect::IsolatedSeededStream: return "isolated_seeded_stream";
    case RngEffect::SharedCallee: return "shared_callee_stream";
    case RngEffect::IsolatedCallee: return "isolated_callee_stream";
    }
    return "unknown";
}
const char* effectCheckName(EffectCheck check) {
    switch (check) {
    case EffectCheck::NumericRange: return "numeric_range";
    case EffectCheck::StreamStackUnderflow: return "stream_stack_underflow";
    case EffectCheck::PartRenderArguments: return "part_render_arguments";
    case EffectCheck::Callee: return "callee";
    case EffectCheck::PartOutputMissing: return "part_output_missing";
    case EffectCheck::DivideByZero: return "divide_by_zero";
    case EffectCheck::IntegerOverflow: return "integer_overflow";
    case EffectCheck::PartOutputContext: return "part_output_context";
    case EffectCheck::IndexRange: return "index_range";
    case EffectCheck::UnassignedHandle: return "unassigned_handle";
    case EffectCheck::ScopeDepth: return "scope_depth";
    }
    return "unknown";
}
const OpcodeSpec* findOpcode(Opcode opcode) { return kByOpcode[static_cast<u8>(opcode)]; }
const OpcodeSpec* findOpcode(std::string_view mnemonic) {
    static const auto names = [] {
        std::map<std::string_view,const OpcodeSpec*,std::less<>> index;
        for (const auto& spec : kSpecs) index.emplace(spec.mnemonic,&spec);
        return index;
    }();
    const auto found = names.find(mnemonic);
    return found == names.end() ? nullptr : found->second;
}

} // namespace iprop
