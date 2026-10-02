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
            if (target.constant) {
                emitConst(*target.constant);
                return target.palette->type;
            }
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
        // Compare the fractional distance before adjusting the integral
        // value: x +/- 0.5 can itself round across the halfway boundary.
        emitArg(call.arguments[0]);
        const auto value = temporary(VT::F32);
        const auto base = temporary(VT::F32);
        ins(Opcode::LocalSet, 0, value.index());
        ins(Opcode::LocalGet, 0, value.index());
        emitConst(0.0f);
        ins(Opcode::F32Ge);
        ins(Opcode::If);
        const auto arm = [&](bool positive) {
            ins(Opcode::LocalGet, 0, value.index());
            ins(positive ? Opcode::F32Floor : Opcode::F32Ceil);
            ins(Opcode::LocalSet, 0, base.index());
            ins(Opcode::LocalGet, 0, positive ? value.index() : base.index());
            ins(Opcode::LocalGet, 0, positive ? base.index() : value.index());
            ins(Opcode::F32Sub);
            emitConst(0.5f);
            ins(Opcode::F32Ge);
            ins(Opcode::If);
            ins(Opcode::LocalGet, 0, base.index());
            emitConst(1.0f);
            ins(positive ? Opcode::F32Add : Opcode::F32Sub);
            ins(Opcode::LocalSet, 0, base.index());
            ins(Opcode::End);
        };
        arm(true);
        ins(Opcode::Else);
        arm(false);
        ins(Opcode::End);
        ins(Opcode::LocalGet, 0, base.index());
        ins(Opcode::F32ToI32);
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
