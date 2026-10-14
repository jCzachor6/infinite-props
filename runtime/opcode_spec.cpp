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
    case Opcode::DistributionBegin: case Opcode::DistributionAppend: case Opcode::DistributionEnd:
        return {R::None,false,false,check(C::NumericRange)};
    case Opcode::SamplerValue: case Opcode::SamplerRange: case Opcode::SamplerWith:
        return {R::None,false,false,check(C::NumericRange)};
    case Opcode::SamplerPick: case Opcode::SamplerPickList:
        return {R::AdvanceCurrent,false,false,static_cast<u16>(check(C::NumericRange)|check(C::UnassignedHandle))};
    case Opcode::ListGet: return {R::None,false,false,static_cast<u16>(check(C::IndexRange)|check(C::UnassignedHandle))};
    case Opcode::ListLen: case Opcode::ListDistinct: case Opcode::ListSort:
        return {R::None,false,false,check(C::UnassignedHandle)};
    case Opcode::LayerBegin: return {R::None,true};
    case Opcode::SurfacePaint: case Opcode::SurfacePaintMask:
        return {R::None,true,false,check(C::UnassignedHandle)};
    case Opcode::MaskFrom: case Opcode::MaskInvert: case Opcode::ClipBegin:
        return {R::None,false,false,check(C::UnassignedHandle)};
    case Opcode::MaskUnion: case Opcode::MaskIntersect: case Opcode::MaskSubtract:
    case Opcode::MaskExpand: case Opcode::MaskShrink: case Opcode::MaskOffset:
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
constexpr OpcodeSpec fixed(Opcode op, const char* name, u16 minor, StackSignature stack) {
    return {op,name,minor,OperandKind::None,AuxPolicy::Zero,stack,effects(op)};
}
constexpr OpcodeSpec dynamic(Opcode op, const char* name, u16 minor, OperandKind operand,
                             AuxPolicy aux = AuxPolicy::Zero) {
    return {op,name,minor,operand,aux,std::nullopt,effects(op)};
}

constexpr OpcodeSpec kSpecs[] = {
    fixed(Opcode::Nop, "nop", 1, StackSignature({})),
    dynamic(Opcode::Block, "block", 1, OperandKind::None),
    dynamic(Opcode::Loop, "loop", 1, OperandKind::None),
    dynamic(Opcode::If, "if", 1, OperandKind::None),
    dynamic(Opcode::Else, "else", 1, OperandKind::None),
    dynamic(Opcode::End, "end", 1, OperandKind::None),
    dynamic(Opcode::Br, "br", 1, OperandKind::Depth),
    dynamic(Opcode::BrIf, "br_if", 1, OperandKind::Depth),
    dynamic(Opcode::Call, "call", 1, OperandKind::Name),
    dynamic(Opcode::Return, "return", 1, OperandKind::None),
    dynamic(Opcode::ConstI32, "const.i32", 1, OperandKind::IntConst),
    dynamic(Opcode::ConstF32, "const.f32", 1, OperandKind::FloatConst),
    dynamic(Opcode::ConstColor, "const.color", 1, OperandKind::ColorConst),
    dynamic(Opcode::LocalGet, "local.get", 1, OperandKind::Index),
    dynamic(Opcode::LocalSet, "local.set", 1, OperandKind::Index),
    dynamic(Opcode::LocalTee, "local.tee", 1, OperandKind::Index),
    dynamic(Opcode::Drop, "drop", 1, OperandKind::None),
    dynamic(Opcode::Dup, "dup", 1, OperandKind::None),
    fixed(Opcode::I32Add, "i32.add", 1, StackSignature({S::I32, S::I32}, V::I32)),
    fixed(Opcode::I32Sub, "i32.sub", 1, StackSignature({S::I32, S::I32}, V::I32)),
    fixed(Opcode::I32Mul, "i32.mul", 1, StackSignature({S::I32, S::I32}, V::I32)),
    fixed(Opcode::I32DivS, "i32.div_s", 1, StackSignature({S::I32, S::I32}, V::I32)),
    fixed(Opcode::I32DivU, "i32.div_u", 1, StackSignature({S::I32, S::I32}, V::I32)),
    fixed(Opcode::I32RemS, "i32.rem_s", 1, StackSignature({S::I32, S::I32}, V::I32)),
    fixed(Opcode::I32RemU, "i32.rem_u", 1, StackSignature({S::I32, S::I32}, V::I32)),
    fixed(Opcode::I32And, "i32.and", 1, StackSignature({S::I32, S::I32}, V::I32)),
    fixed(Opcode::I32Or, "i32.or", 1, StackSignature({S::I32, S::I32}, V::I32)),
    fixed(Opcode::I32Xor, "i32.xor", 1, StackSignature({S::I32, S::I32}, V::I32)),
    fixed(Opcode::I32Shl, "i32.shl", 1, StackSignature({S::I32, S::I32}, V::I32)),
    fixed(Opcode::I32ShrS, "i32.shr_s", 1, StackSignature({S::I32, S::I32}, V::I32)),
    fixed(Opcode::I32ShrU, "i32.shr_u", 1, StackSignature({S::I32, S::I32}, V::I32)),
    fixed(Opcode::I32Neg, "i32.neg", 1, StackSignature({S::I32}, V::I32)),
    fixed(Opcode::I32Not, "i32.not", 1, StackSignature({S::I32}, V::I32)),
    fixed(Opcode::I32Eq, "i32.eq", 1, StackSignature({S::I32, S::I32}, V::I32)),
    fixed(Opcode::I32Ne, "i32.ne", 1, StackSignature({S::I32, S::I32}, V::I32)),
    fixed(Opcode::I32LtS, "i32.lt_s", 1, StackSignature({S::I32, S::I32}, V::I32)),
    fixed(Opcode::I32LtU, "i32.lt_u", 1, StackSignature({S::I32, S::I32}, V::I32)),
    fixed(Opcode::I32GtS, "i32.gt_s", 1, StackSignature({S::I32, S::I32}, V::I32)),
    fixed(Opcode::I32GtU, "i32.gt_u", 1, StackSignature({S::I32, S::I32}, V::I32)),
    fixed(Opcode::I32LeS, "i32.le_s", 1, StackSignature({S::I32, S::I32}, V::I32)),
    fixed(Opcode::I32LeU, "i32.le_u", 1, StackSignature({S::I32, S::I32}, V::I32)),
    fixed(Opcode::I32GeS, "i32.ge_s", 1, StackSignature({S::I32, S::I32}, V::I32)),
    fixed(Opcode::I32GeU, "i32.ge_u", 1, StackSignature({S::I32, S::I32}, V::I32)),
    fixed(Opcode::F32Add, "f32.add", 1, StackSignature({S::F32, S::F32}, V::F32)),
    fixed(Opcode::F32Sub, "f32.sub", 1, StackSignature({S::F32, S::F32}, V::F32)),
    fixed(Opcode::F32Mul, "f32.mul", 1, StackSignature({S::F32, S::F32}, V::F32)),
    fixed(Opcode::F32Div, "f32.div", 1, StackSignature({S::F32, S::F32}, V::F32)),
    fixed(Opcode::F32Eq, "f32.eq", 1, StackSignature({S::F32, S::F32}, V::I32)),
    fixed(Opcode::F32Ne, "f32.ne", 1, StackSignature({S::F32, S::F32}, V::I32)),
    fixed(Opcode::F32Lt, "f32.lt", 1, StackSignature({S::F32, S::F32}, V::I32)),
    fixed(Opcode::F32Gt, "f32.gt", 1, StackSignature({S::F32, S::F32}, V::I32)),
    fixed(Opcode::F32Le, "f32.le", 1, StackSignature({S::F32, S::F32}, V::I32)),
    fixed(Opcode::F32Ge, "f32.ge", 1, StackSignature({S::F32, S::F32}, V::I32)),
    fixed(Opcode::F32Neg, "f32.neg", 1, StackSignature({S::F32}, V::F32)),
    fixed(Opcode::F32Abs, "f32.abs", 1, StackSignature({S::F32}, V::F32)),
    fixed(Opcode::F32Sqrt, "f32.sqrt", 1, StackSignature({S::F32}, V::F32)),
    fixed(Opcode::I32ToF32, "i32.to.f32", 1, StackSignature({S::I32}, V::F32)),
    fixed(Opcode::F32ToI32, "f32.to.i32", 1, StackSignature({S::F32}, V::I32)),
    fixed(Opcode::F32Sin, "f32.sin", 2, StackSignature({S::F32}, V::F32)),
    fixed(Opcode::ColorMix, "color.mix", 1, StackSignature({S::Color, S::Color, S::F32}, V::Color)),
    fixed(Opcode::ColorShade, "color.shade", 1, StackSignature({S::Color, S::I32}, V::Color)),
    fixed(Opcode::ColorSetA, "color.set.a", 3, StackSignature({S::Color, S::I32}, V::Color)),
    fixed(Opcode::F32Cos, "f32.cos", 2, StackSignature({S::F32}, V::F32)),
    fixed(Opcode::F32Floor, "f32.floor", 2, StackSignature({S::F32}, V::F32)),
    fixed(Opcode::F32Ceil, "f32.ceil", 2, StackSignature({S::F32}, V::F32)),
    fixed(Opcode::F32Min, "f32.min", 2, StackSignature({S::F32, S::F32}, V::F32)),
    fixed(Opcode::F32Max, "f32.max", 2, StackSignature({S::F32, S::F32}, V::F32)),
    fixed(Opcode::F32Pow, "f32.pow", 2, StackSignature({S::F32, S::F32}, V::F32)),
    fixed(Opcode::F32Exp, "f32.exp", 2, StackSignature({S::F32}, V::F32)),
    fixed(Opcode::F32Atan2, "f32.atan2", 2, StackSignature({S::F32, S::F32}, V::F32)),
    fixed(Opcode::Vec2Make, "vec2.make", 3, StackSignature({S::F32, S::F32}, V::Vec2)),
    fixed(Opcode::Vec2Add, "vec2.add", 3, StackSignature({S::Vec2, S::Vec2}, V::Vec2)),
    fixed(Opcode::Vec2Sub, "vec2.sub", 3, StackSignature({S::Vec2, S::Vec2}, V::Vec2)),
    fixed(Opcode::Vec2Mul, "vec2.mul", 3, StackSignature({S::Vec2, S::ScalarOrVec2}, V::Vec2)),
    fixed(Opcode::Vec2Div, "vec2.div", 3, StackSignature({S::Vec2, S::ScalarOrVec2}, V::Vec2)),
    fixed(Opcode::StreamPush, "stream.push", 1, StackSignature({S::I32})),
    fixed(Opcode::StreamPop, "stream.pop", 1, StackSignature({})),
    fixed(Opcode::RngUnit, "rng.unit", 1, StackSignature({}, V::F32)),
    fixed(Opcode::RngRange, "rng.range", 1, StackSignature({S::F32, S::F32}, V::F32)),
    fixed(Opcode::RngIndex, "rng.index", 1, StackSignature({S::I32}, V::I32)),
    fixed(Opcode::Hash, "hash", 1, StackSignature({S::I32}, V::I32)),
    fixed(Opcode::StreamState, "stream.state", 1, StackSignature({}, V::I32)),
    fixed(Opcode::Noise, "noise", 1, StackSignature({S::F32, S::F32, S::I32}, V::F32)),
    fixed(Opcode::Fault, "fault", 1, StackSignature({S::F32, S::F32, S::I32}, V::F32)),
    fixed(Opcode::Smooth, "smooth", 1, StackSignature({S::F32}, V::F32)),
    fixed(Opcode::Lerp, "lerp", 1, StackSignature({S::F32, S::F32, S::F32}, V::F32)),
    fixed(Opcode::Vec2Neg, "vec2.neg", 3, StackSignature({S::Vec2}, V::Vec2)),
    fixed(Opcode::Vec2Len, "vec2.len", 3, StackSignature({S::Vec2}, V::F32)),
    fixed(Opcode::Vec2Dot, "vec2.dot", 3, StackSignature({S::Vec2, S::Vec2}, V::F32)),
    fixed(Opcode::Vec2Rot, "vec2.rot", 3, StackSignature({S::Vec2, S::F32}, V::Vec2)),
    fixed(Opcode::Vec2X, "vec2.x", 3, StackSignature({S::Vec2}, V::F32)),
    fixed(Opcode::Vec2Y, "vec2.y", 3, StackSignature({S::Vec2}, V::F32)),
    fixed(Opcode::DistCircle, "dist.circle", 3, StackSignature({S::Vec2, S::Vec2, S::F32}, V::F32)),
    fixed(Opcode::DistRect, "dist.rect", 3, StackSignature({S::Vec2, S::Vec2, S::Vec2}, V::F32)),
    fixed(Opcode::DistRoundRect, "dist.roundrect", 3, StackSignature({S::Vec2, S::Vec2, S::Vec2, S::F32}, V::F32)),
    fixed(Opcode::DistLine, "dist.line", 3, StackSignature({S::Vec2, S::Vec2, S::Vec2}, V::F32)),
    fixed(Opcode::DistBezier, "dist.bezier", 3, StackSignature({S::Vec2, S::Vec2, S::Vec2, S::Vec2}, V::F32)),
    fixed(Opcode::Fill, "fill", 3, StackSignature({S::F32, S::F32}, V::F32)),
    fixed(Opcode::Pixel, "pixel", 1, StackSignature({S::I32, S::I32, S::Color})),
    fixed(Opcode::Rect, "rect", 1, StackSignature({S::F32, S::F32, S::F32, S::F32, S::Color})),
    fixed(Opcode::Line, "line", 1, StackSignature({S::F32, S::F32, S::F32, S::F32, S::I32, S::Color})),
    dynamic(Opcode::Poly, "poly", 1, OperandKind::Count),
    fixed(Opcode::Oval, "oval", 1, StackSignature({S::F32, S::F32, S::F32, S::F32, S::Color})),
    fixed(Opcode::Weather, "weather", 1, StackSignature({S::I32, S::I32})),
    fixed(Opcode::Curve, "curve", 3, StackSignature({S::Vec2, S::Vec2, S::Vec2, S::F32, S::Color})),
    fixed(Opcode::CurveCubic, "curve.cubic", 3, StackSignature({S::Vec2, S::Vec2, S::Vec2, S::Vec2, S::F32, S::Color})),
    fixed(Opcode::DistBezierCubic, "dist.bezier.cubic", 3, StackSignature({S::Vec2, S::Vec2, S::Vec2, S::Vec2, S::Vec2}, V::F32)),
    fixed(Opcode::NoiseGrad, "noise.grad", 3, StackSignature({S::F32, S::F32, S::I32}, V::F32)),
    fixed(Opcode::NoiseFbm, "noise.fbm", 3, StackSignature({S::F32, S::F32, S::I32, S::I32}, V::F32)),
    fixed(Opcode::NoiseCell, "noise.cell", 3, StackSignature({S::F32, S::F32, S::I32}, V::F32)),
    fixed(Opcode::Warp, "warp", 3, StackSignature({S::F32, S::F32, S::F32, S::I32}, V::F32)),
    fixed(Opcode::CellJitter, "cell.jitter", 3, StackSignature({S::I32, S::I32, S::I32}, V::Vec2)),
    fixed(Opcode::NoiseRidge, "noise.ridge", 3, StackSignature({S::F32, S::F32, S::I32, S::I32}, V::F32)),
    fixed(Opcode::Gain, "gain", 3, StackSignature({S::F32, S::F32}, V::F32)),
    dynamic(Opcode::OutPoint, "out.point", 1, OperandKind::Name),
    dynamic(Opcode::OutRect, "out.rect", 1, OperandKind::Name),
    dynamic(Opcode::OutCircle, "out.circle", 1, OperandKind::Name),
    dynamic(Opcode::OutListPoint, "out.list.point", 1, OperandKind::Name),
    dynamic(Opcode::OutListRect, "out.list.rect", 1, OperandKind::Name),
    dynamic(Opcode::OutListCircle, "out.list.circle", 1, OperandKind::Name),
    dynamic(Opcode::PartGetPoint, "part.get.point", 1, OperandKind::Name),
    dynamic(Opcode::PartGetRect, "part.get.rect", 1, OperandKind::Name),
    dynamic(Opcode::PartGetCircle, "part.get.circle", 1, OperandKind::Name),
    fixed(Opcode::Quintic, "quintic", 3, StackSignature({S::F32}, V::F32)),
    fixed(Opcode::ColorMul, "color.mul", 3, StackSignature({S::Color, S::Color}, V::Color)),
    fixed(Opcode::ColorScale, "color.scale", 3, StackSignature({S::Color, S::F32}, V::Color)),
    fixed(Opcode::ColorHsv, "color.hsv", 3, StackSignature({S::Color, S::F32, S::F32, S::F32}, V::Color)),
    fixed(Opcode::Dither, "dither", 3, StackSignature({S::F32, S::I32, S::I32}, V::F32)),
    fixed(Opcode::Vec2Fold, "vec2.fold", 3, StackSignature({S::Vec2, S::I32}, V::Vec2)),
    fixed(Opcode::Vec2Polar, "vec2.polar", 3, StackSignature({S::Vec2}, V::Vec2)),
    dynamic(Opcode::PartDraw, "part.draw", 1, OperandKind::Name, AuxPolicy::PartTransform),
    fixed(Opcode::Smoothstep, "smoothstep", 3, StackSignature({S::F32, S::F32, S::F32}, V::F32)),
    fixed(Opcode::Remap, "remap", 3, StackSignature({S::F32, S::F32, S::F32, S::F32, S::F32}, V::F32)),
    fixed(Opcode::Clamp, "clamp", 3, StackSignature({S::F32, S::F32, S::F32}, V::F32)),
    fixed(Opcode::ColorR, "color.r", 5, StackSignature({S::Color}, V::I32)),
    fixed(Opcode::ColorG, "color.g", 5, StackSignature({S::Color}, V::I32)),
    fixed(Opcode::ColorB, "color.b", 5, StackSignature({S::Color}, V::I32)),
    fixed(Opcode::ColorA, "color.a", 5, StackSignature({S::Color}, V::I32)),
    fixed(Opcode::RngInt, "rng.int", 5, StackSignature({S::I32, S::I32}, V::I32)),
    fixed(Opcode::RngFloat, "rng.float", 5, StackSignature({S::F32, S::F32}, V::F32)),
    fixed(Opcode::DistributionGaussian, "distribution.gaussian", 5,
          StackSignature({S::F32, S::F32}, V::Distribution)),
    fixed(Opcode::DistributionTriangular, "distribution.triangular", 5,
          StackSignature({S::F32, S::F32, S::F32}, V::Distribution)),
    fixed(Opcode::DistributionBeta, "distribution.beta", 5,
          StackSignature({S::F32, S::F32, S::F32, S::F32}, V::Distribution)),
    fixed(Opcode::RngIntWith, "rng.int.with", 5, StackSignature({S::I32, S::I32, S::Distribution}, V::I32)),
    fixed(Opcode::RngFloatWith, "rng.float.with", 5, StackSignature({S::F32, S::F32, S::Distribution}, V::F32)),
    fixed(Opcode::ColorMixRgba, "color.mix.rgba", 5, StackSignature({S::Color, S::Color, S::F32}, V::Color)),
    fixed(Opcode::Vec2Mix, "vec2.mix", 5, StackSignature({S::Vec2, S::Vec2, S::F32}, V::Vec2)),
    fixed(Opcode::I32Min, "i32.min", 6, StackSignature({S::I32, S::I32}, V::I32)),
    fixed(Opcode::I32Max, "i32.max", 6, StackSignature({S::I32, S::I32}, V::I32)),
    fixed(Opcode::I32Clamp, "i32.clamp", 6, StackSignature({S::I32, S::I32, S::I32}, V::I32)),
    fixed(Opcode::Vec2Normalize, "vec2.normalize", 6, StackSignature({S::Vec2}, V::Vec2)),
    dynamic(Opcode::ConstList, "const.list", 6, OperandKind::Index),
    dynamic(Opcode::ListBegin, "list.begin", 6, OperandKind::Index),
    dynamic(Opcode::ListAppend, "list.append", 6, OperandKind::Index),
    dynamic(Opcode::ListEnd, "list.end", 6, OperandKind::Index),
    dynamic(Opcode::ListGet, "list.get", 6, OperandKind::Index),
    dynamic(Opcode::ListLen, "list.len", 6, OperandKind::Index),
    dynamic(Opcode::PolyList, "poly.list", 6, OperandKind::None),
    dynamic(Opcode::SamplerValue, "sampler.value", 6, OperandKind::Index),
    dynamic(Opcode::SamplerRange, "sampler.range", 6, OperandKind::Index),
    dynamic(Opcode::SamplerWith, "sampler.with", 6, OperandKind::Index),
    dynamic(Opcode::SamplerPick, "sampler.pick", 6, OperandKind::Index),
    dynamic(Opcode::SamplerPickList, "sampler.pick.list", 6, OperandKind::Index),
    dynamic(Opcode::ConstSampler, "const.sampler", 6, OperandKind::Index),
    dynamic(Opcode::ConstDistribution, "const.distribution", 6, OperandKind::Index),
    dynamic(Opcode::DistributionBegin, "distribution.begin", 6, OperandKind::Index),
    dynamic(Opcode::DistributionAppend, "distribution.append", 6, OperandKind::Index),
    dynamic(Opcode::DistributionEnd, "distribution.end", 6, OperandKind::None),
    dynamic(Opcode::LayerBegin, "layer.begin", 6, OperandKind::Index, AuxPolicy::SurfaceMode),
    dynamic(Opcode::SurfacePaint, "surface.paint", 6, OperandKind::Index),
    dynamic(Opcode::MaskFrom, "mask.from", 6, OperandKind::None),
    dynamic(Opcode::MaskUnion, "mask.union", 6, OperandKind::None),
    dynamic(Opcode::MaskIntersect, "mask.intersect", 6, OperandKind::None),
    dynamic(Opcode::MaskSubtract, "mask.subtract", 6, OperandKind::None),
    dynamic(Opcode::MaskInvert, "mask.invert", 6, OperandKind::None),
    dynamic(Opcode::MaskExpand, "mask.expand", 6, OperandKind::None),
    dynamic(Opcode::MaskShrink, "mask.shrink", 6, OperandKind::None),
    dynamic(Opcode::MaskOffset, "mask.offset", 6, OperandKind::None),
    dynamic(Opcode::SurfacePaintMask, "surface.paint.mask", 6, OperandKind::Index),
    dynamic(Opcode::ClipBegin, "clip.begin", 6, OperandKind::None),
    dynamic(Opcode::OutMask, "out.mask", 6, OperandKind::Index),
    dynamic(Opcode::OutLayer, "out.layer", 6, OperandKind::Index),
    dynamic(Opcode::PartGetMask, "part.get.mask", 6, OperandKind::Index),
    dynamic(Opcode::PartGetLayer, "part.get.layer", 6, OperandKind::Index),
    dynamic(Opcode::ListDistinct, "list.distinct", 6, OperandKind::Index),
    dynamic(Opcode::ListSort, "list.sort", 6, OperandKind::Index),
};
constexpr auto kByOpcode = [] {
    std::array<const OpcodeSpec*,256> index{};
    for (const auto& spec : kSpecs) index[static_cast<u8>(spec.opcode)] = &spec;
    return index;
}();
} // namespace

std::span<const OpcodeSpec> opcodeSpecs() { return kSpecs; }
u16 writtenMinor(u16 minor, Opcode opcode) {
    const auto* spec = findOpcode(opcode);
    return spec && spec->minimumMinor > minor ? spec->minimumMinor : minor;
}
u16 writtenMinor(u16 minor, ValueType type) {
    return type.needsDescriptor() && minor < 6 ? u16{6} : type == ValueType::Distribution && minor < 5 ? u16{5} : minor;
}
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
