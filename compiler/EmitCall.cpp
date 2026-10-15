#include "Emit.hpp"
#include "PaletteLowerer.hpp"

#include <type_traits>

namespace iprop::compiler {

ips::ValueType Emitter::emitCall(const ips::Call& call) {
    const auto& resolved = current_->calls.at(&call);
    return std::visit([&](const auto& target) -> ips::ValueType {
        using T = std::decay_t<decltype(target)>;
        if constexpr (std::is_same_v<T, BuiltinCall>) return emitBuiltin(target);
        else if constexpr (std::is_same_v<T, SelectionCall>) return emitSelection(target);
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

void Emitter::emitOverload(const ips::BuiltinOverload& overload) {
    if (!overload.opcode) return; // an identity conversion
    switch (operandKind(*overload.opcode)) {
    case OperandKind::None:
        ins(*overload.opcode);
        return;
    case OperandKind::TypeDescriptor:
        ins(*overload.opcode, 0, plan_.pools.addType(overload.params.front().type));
        return;
    default:
        throw std::logic_error("builtin overload operand requires its own lowering");
    }
}

ips::ValueType Emitter::emitMethod(const ips::Expr& expr) {
    const auto* overload = current_->expression(expr).overload;
    if (!overload) throw std::logic_error("method expression without a resolved overload");
    emitOverload(*overload);
    return overload->result;
}

ips::ValueType Emitter::emitBuiltin(const BuiltinCall& call) {
    using VT = ips::ValueType;
    const auto& spec = *call.spec;
    if (spec.lowering == ips::BuiltinLowering::SurfacePaint) {
        // The overload's blend parameter is the static mode operand; the other
        // parameters are stack values, in their (unchanged) parameter order.
        const auto& params = call.overload->params;
        if (params.size() != call.arguments.size()) throw std::logic_error("surface.paint arguments differ from their overload");
        auto mode = CompositionMode::Inherited;
        for (std::size_t i = 0; i < params.size(); ++i) {
            if (params[i].name == "blend") mode = ips::surfaceMode(*call.arguments[i].expression)->mode;
            else emitArg(call.arguments[i]);
        }
        ins(*call.overload->opcode, 0, static_cast<u16>(mode));
        return VT::Void;
    }
    // An overload with an instruction (or an identity conversion) lowers
    // generically; the special lowerings below expand to several.
    if (call.overload && (call.overload->opcode || spec.lowering == ips::BuiltinLowering::Opcode)) {
        for (const auto& arg : call.arguments) emitArg(arg);
        emitOverload(*call.overload);
        return call.overload->result;
    }
    if (spec.lowering == ips::BuiltinLowering::Beta) {
        for (const auto& arg : call.arguments) emitArg(arg);
        emitConst(0.0f); // the default natural range 0-1
        emitConst(1.0f);
        ins(Opcode::DistributionBeta);
        return VT::Distribution;
    }
    if (spec.lowering == ips::BuiltinLowering::Chance) {
        emitArg(call.arguments[0]);
        ins(Opcode::RngUnit);
        ins(Opcode::F32Gt);
        return VT::I32;
    }
    if (spec.lowering == ips::BuiltinLowering::Roll) {
        emitArg(call.arguments[0]);
        const auto seed = temporary(VT::I32);
        ins(Opcode::LocalSet, 0, seed.index());
        emitArg(call.arguments[1]);
        emitArg(call.arguments[2]);
        ins(Opcode::LocalGet, 0, seed.index());
        ins(Opcode::StreamPush);
        ins(Opcode::RngRange);
        ins(Opcode::StreamPop);
        return VT::F32;
    }
    if (spec.lowering == ips::BuiltinLowering::ColorConstructor) {
        // Each mask leaves the other channels at 255. Integer shade/mul
        // builds RGB exactly, without a VM opcode or float-rounding errors.
        const Color masks[] = {{0, 255, 255, 255}, {255, 0, 255, 255}, {255, 255, 0, 255}};
        for (std::size_t i = 0; i < 3; ++i) {
            emitConst(masks[i]);
            emitArg(call.arguments[i]);
            ins(Opcode::Dup);
            emitConst(i32{0});
            ins(Opcode::I32LtS);
            ins(Opcode::If);
            ins(Opcode::Drop);
            emitConst(i32{0});
            ins(Opcode::End);
            ins(Opcode::ColorShade);
            if (i != 0) ins(Opcode::ColorMul);
        }
        if (call.arguments.size() == 4) {
            emitArg(call.arguments[3]);
            ins(Opcode::ColorSetA);
        }
        return VT::Color;
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
    // The variadic polygon: vertex pairs, then the colour.
    for (const auto& arg : call.arguments) emitArg(arg);
    const u16 vertices = static_cast<u16>((call.arguments.size() - 1) / 2);
    ins(Opcode::Poly, 0, vertices);
    return VT::Void;
}

void Emitter::emitPartCall(const PartCall& call) {
    for (const auto& argument : call.arguments) {
        if (const auto* arg = std::get_if<PlannedArgument>(&argument)) emitArg(*arg);
        else if (const auto* at = std::get_if<PartCall::Placement>(&argument)) emitPointValue(*at->expression);
        else emitConst(std::get<Value>(argument));
    }
    ins(Opcode::PartDraw, call.transform.kind(), static_cast<u16>(call.part));
}

} // namespace iprop::compiler
