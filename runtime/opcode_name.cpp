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

    // Colour (§9.5)
    case Opcode::ColorMix: return "color.mix";
    case Opcode::ColorShade: return "color.shade";

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
    case Opcode::Smooth: return "smooth";
    case Opcode::Lerp: return "lerp";

    // Canvas (§9.8)
    case Opcode::Pixel: return "pixel";
    case Opcode::Rect: return "rect";
    case Opcode::Line: return "line";
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
