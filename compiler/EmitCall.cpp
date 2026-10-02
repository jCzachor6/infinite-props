#include "Emit.hpp"
#include "PaletteLowerer.hpp"

#include <type_traits>

namespace iprop::compiler {

ips::ValueType Emitter::emitCall(const ips::Call& call) {
    const auto& resolved = current_->calls.at(&call);
    return std::visit([&](const auto& target) -> ips::ValueType {
        using T = std::decay_t<decltype(target)>;
        if constexpr (std::is_same_v<T, BuiltinCall>) return emitBuiltin(target);
        else if constexpr (std::is_same_v<T, UserCall>) {
            for (const auto& arg : target.arguments) emitArg(arg);
            ins(Opcode::Call, 0, static_cast<u16>(target.function));
            return target.result;
        } else if constexpr (std::is_same_v<T, PartCall>) {
            emitPartCall(target);
            return ips::ValueType::Void;
        } else {
            return PaletteLowerer(*this, *target.palette).ramp(target.position);
        }
    }, resolved.target);
}

void Emitter::emitArg(const PlannedArgument& arg) {
    emitExpr(*arg.expression);
    if (arg.widen) ins(Opcode::I32ToF32);
}

ips::ValueType Emitter::emitBuiltin(const BuiltinCall& call) {
    using VT = ips::ValueType;
    const auto& spec = *call.spec;
    if (spec.lowering == ips::BuiltinLowering::Opcode) {
        for (const auto& arg : call.arguments) emitArg(arg);
        ins(*call.overload->opcode);
        return spec.result;
    }
    if (spec.lowering == ips::BuiltinLowering::Round) {
        // round-half-away-from-zero (std::round semantics, spec 8.1):
        // x >= 0 ? trunc(x + 0.5) : -trunc(-x + 0.5). The value is kept in
        // the f32 scratch (blocks have no results, so the arms store the
        // outcome in the i32 scratch instead of yielding it).
        // The i32 scratch takes the lower index (appended first).
        emitArg(call.arguments[0]);
        const auto result = temporary(VT::I32);
        const auto value = temporary(VT::F32);
        const u16 i32Slot = result.index();
        const u16 f32Slot = value.index();
        ins(Opcode::LocalSet, 0, f32Slot);
        ins(Opcode::LocalGet, 0, f32Slot);
        ins(Opcode::ConstF32, 0, static_cast<u16>(plan_.pools.addFloat(0)));
        ins(Opcode::F32Ge);
        ins(Opcode::If);
        ins(Opcode::LocalGet, 0, f32Slot);
        ins(Opcode::ConstF32, 0,
            static_cast<u16>(plan_.pools.addFloat(0x3F000000u))); // 0.5
        ins(Opcode::F32Add);
        ins(Opcode::F32ToI32);
        ins(Opcode::LocalSet, 0, i32Slot);
        ins(Opcode::Else);
        ins(Opcode::LocalGet, 0, f32Slot);
        ins(Opcode::F32Neg);
        ins(Opcode::ConstF32, 0,
            static_cast<u16>(plan_.pools.addFloat(0x3F000000u)));
        ins(Opcode::F32Add);
        ins(Opcode::F32ToI32);
        ins(Opcode::I32Neg);
        ins(Opcode::LocalSet, 0, i32Slot);
        ins(Opcode::End);
        ins(Opcode::LocalGet, 0, i32Slot);
        return VT::I32;
    }
    for (const auto& arg : call.arguments) emitArg(arg);
    const u16 vertices = static_cast<u16>((call.arguments.size() - 1) / 2);
    ins(Opcode::Poly, 0, vertices);
    return VT::Void;
}

void Emitter::emitPartCall(const PartCall& call) {
    for (const auto& uniform : call.uniforms) {
        if (const auto* arg = std::get_if<PlannedArgument>(&uniform)) emitArg(*arg);
        else emitConst(std::get<Value>(uniform));
    }
    for (const auto& arg : call.positional) emitArg(arg);
    emitPointValue(*call.at);
    ins(Opcode::PartDraw, call.transform.kind(), static_cast<u16>(call.part));
}

} // namespace iprop::compiler
