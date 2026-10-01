#include "iprop/Instruction.hpp"

#include "iprop/Constants.hpp"

namespace iprop {

const char* opcodeMnemonic(Opcode opcode) {
    switch (opcode) {
    // Control (§9.1)
    case Opcode::Nop: return "nop";
    case Opcode::Block: return "block";
    case Opcode::Loop: return "loop";
    case Opcode::If: return "if";
    case Opcode::Else: return "else";
    case Opcode::End: return "end";
    case Opcode::Br: return "br";
    case Opcode::BrIf: return "br_if";
    case Opcode::Call: return "call";
    case Opcode::Return: return "return";

    // Stack and constants (§9.2)
    case Opcode::ConstI32: return "const.i32";
    case Opcode::ConstF32: return "const.f32";
    case Opcode::ConstColor: return "const.color";
    case Opcode::LocalGet: return "local.get";
    case Opcode::LocalSet: return "local.set";
    case Opcode::LocalTee: return "local.tee";
    case Opcode::Drop: return "drop";
    case Opcode::Dup: return "dup";

    // Integer (§9.3)
    case Opcode::I32Add: return "i32.add";
    case Opcode::I32Sub: return "i32.sub";
    case Opcode::I32Mul: return "i32.mul";
    case Opcode::I32DivS: return "i32.div_s";
    case Opcode::I32DivU: return "i32.div_u";
    case Opcode::I32RemS: return "i32.rem_s";
    case Opcode::I32RemU: return "i32.rem_u";
    case Opcode::I32And: return "i32.and";
    case Opcode::I32Or: return "i32.or";
    case Opcode::I32Xor: return "i32.xor";
    case Opcode::I32Shl: return "i32.shl";
    case Opcode::I32ShrS: return "i32.shr_s";
    case Opcode::I32ShrU: return "i32.shr_u";
    case Opcode::I32Neg: return "i32.neg";
    case Opcode::I32Not: return "i32.not";
    case Opcode::I32Eq: return "i32.eq";
    case Opcode::I32Ne: return "i32.ne";
    case Opcode::I32LtS: return "i32.lt_s";
    case Opcode::I32LtU: return "i32.lt_u";
    case Opcode::I32GtS: return "i32.gt_s";
    case Opcode::I32GtU: return "i32.gt_u";
    case Opcode::I32LeS: return "i32.le_s";
    case Opcode::I32LeU: return "i32.le_u";
    case Opcode::I32GeS: return "i32.ge_s";
    case Opcode::I32GeU: return "i32.ge_u";

    // Float (§9.4)
    case Opcode::F32Add: return "f32.add";
    case Opcode::F32Sub: return "f32.sub";
    case Opcode::F32Mul: return "f32.mul";
    case Opcode::F32Div: return "f32.div";
    case Opcode::F32Eq: return "f32.eq";
    case Opcode::F32Ne: return "f32.ne";
    case Opcode::F32Lt: return "f32.lt";
    case Opcode::F32Gt: return "f32.gt";
    case Opcode::F32Le: return "f32.le";
    case Opcode::F32Ge: return "f32.ge";
    case Opcode::F32Neg: return "f32.neg";
    case Opcode::F32Abs: return "f32.abs";
    case Opcode::F32Sqrt: return "f32.sqrt";
    case Opcode::I32ToF32: return "i32.to.f32";
    case Opcode::F32ToI32: return "f32.to.i32";
    case Opcode::F32Sin: return "f32.sin";
    case Opcode::F32Cos: return "f32.cos";
    case Opcode::F32Floor: return "f32.floor";
    case Opcode::F32Ceil: return "f32.ceil";
    case Opcode::F32Min: return "f32.min";
    case Opcode::F32Max: return "f32.max";
    case Opcode::F32Pow: return "f32.pow";
    case Opcode::F32Exp: return "f32.exp";
    case Opcode::F32Atan2: return "f32.atan2";
    case Opcode::Vec2Make: return "vec2.make";
    case Opcode::Vec2Add: return "vec2.add";
    case Opcode::Vec2Sub: return "vec2.sub";
    case Opcode::Vec2Mul: return "vec2.mul";
    case Opcode::Vec2Div: return "vec2.div";
    case Opcode::Vec2Neg: return "vec2.neg";
    case Opcode::Vec2Len: return "vec2.len";
    case Opcode::Vec2Dot: return "vec2.dot";
    case Opcode::Vec2Rot: return "vec2.rot";
    case Opcode::Vec2X: return "vec2.x";
    case Opcode::Vec2Y: return "vec2.y";
    case Opcode::DistCircle: return "dist.circle";
    case Opcode::DistRect: return "dist.rect";
    case Opcode::DistRoundRect: return "dist.roundrect";
    case Opcode::DistLine: return "dist.line";
    case Opcode::DistBezier: return "dist.bezier";
    case Opcode::DistBezierCubic: return "dist.bezier.cubic";
    case Opcode::Fill: return "fill";
    case Opcode::Gain: return "gain";
    case Opcode::Quintic: return "quintic";
    case Opcode::Dither: return "dither";
    case Opcode::Vec2Fold: return "vec2.fold";
    case Opcode::Vec2Polar: return "vec2.polar";

    // Colour (§9.5)
    case Opcode::ColorMix: return "color.mix";
    case Opcode::ColorShade: return "color.shade";
    case Opcode::ColorSetA: return "color.set.a";
    case Opcode::ColorMul: return "color.mul";
    case Opcode::ColorScale: return "color.scale";
    case Opcode::ColorHsv: return "color.hsv";

    // Seeded primitives (§9.6)
    case Opcode::StreamPush: return "stream.push";
    case Opcode::StreamPop: return "stream.pop";
    case Opcode::RngUnit: return "rng.unit";
    case Opcode::RngRange: return "rng.range";
    case Opcode::RngIndex: return "rng.index";
    case Opcode::Hash: return "hash";
    case Opcode::StreamState: return "stream.state";

    // Noise and helpers (§9.7)
    case Opcode::Noise: return "noise";
    case Opcode::Fault: return "fault";
    case Opcode::NoiseGrad: return "noise.grad";
    case Opcode::NoiseFbm: return "noise.fbm";
    case Opcode::NoiseCell: return "noise.cell";
    case Opcode::Warp: return "warp";
    case Opcode::CellJitter: return "cell.jitter";
    case Opcode::NoiseRidge: return "noise.ridge";
    case Opcode::Smooth: return "smooth";
    case Opcode::Lerp: return "lerp";

    // Canvas (§9.8)
    case Opcode::Pixel: return "pixel";
    case Opcode::Rect: return "rect";
    case Opcode::Line: return "line";
    case Opcode::Curve: return "curve";
    case Opcode::CurveCubic: return "curve.cubic";
    case Opcode::Poly: return "poly";
    case Opcode::Oval: return "oval";
    case Opcode::Weather: return "weather";

    // Structured outputs (§9.9)
    case Opcode::OutPoint: return "out.point";
    case Opcode::OutRect: return "out.rect";
    case Opcode::OutCircle: return "out.circle";
    case Opcode::OutListPoint: return "out.list.point";
    case Opcode::OutListRect: return "out.list.rect";
    case Opcode::OutListCircle: return "out.list.circle";
    case Opcode::PartGetPoint: return "part.get.point";
    case Opcode::PartGetRect: return "part.get.rect";
    case Opcode::PartGetCircle: return "part.get.circle";

    // Parts (§9.10)
    case Opcode::PartDraw: return "part.draw";
    default: return nullptr; // reserved opcodes have no mnemonic
    }
}

OperandKind operandKind(Opcode opcode) {
    switch (opcode) {
    case Opcode::ConstI32: return OperandKind::IntConst;
    case Opcode::ConstF32: return OperandKind::FloatConst;
    case Opcode::ConstColor: return OperandKind::ColorConst;
    case Opcode::LocalGet:
    case Opcode::LocalSet:
    case Opcode::LocalTee:
        return OperandKind::Index;
    case Opcode::Br:
    case Opcode::BrIf:
        return OperandKind::Depth;
    case Opcode::Poly:
        return OperandKind::Count;
    case Opcode::Call:
    case Opcode::PartDraw:
    case Opcode::OutPoint:
    case Opcode::OutRect:
    case Opcode::OutCircle:
    case Opcode::OutListPoint:
    case Opcode::OutListRect:
    case Opcode::OutListCircle:
    case Opcode::PartGetPoint:
    case Opcode::PartGetRect:
    case Opcode::PartGetCircle:
        return OperandKind::Name;
    default:
        return OperandKind::None;
    }
}

} // namespace iprop
