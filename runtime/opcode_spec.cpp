#include "iprop/OpcodeSpec.hpp"
#include <map>

namespace iprop {
namespace {
using S = StackType;
using V = ValueType;
constexpr OpcodeSpec fixed(Opcode op, const char* name, u16 minor, StackSignature stack) {
    return {op,name,minor,OperandKind::None,AuxPolicy::Zero,stack};
}
constexpr OpcodeSpec dynamic(Opcode op, const char* name, u16 minor, OperandKind operand,
                             AuxPolicy aux = AuxPolicy::Zero) {
    return {op,name,minor,operand,aux,std::nullopt};
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
};
constexpr auto kByOpcode = [] {
    std::array<const OpcodeSpec*,256> index{};
    for (const auto& spec : kSpecs) index[static_cast<u8>(spec.opcode)] = &spec;
    return index;
}();
} // namespace

std::span<const OpcodeSpec> opcodeSpecs() { return kSpecs; }
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
