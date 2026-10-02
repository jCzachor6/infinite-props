#include "PaletteLowerer.hpp"
#include "Emit.hpp"
#include "ips/Fold.hpp"

namespace iprop::compiler {

ips::ValueType PaletteLowerer::index(const ips::Expr& index) {
    const std::size_t count = palette_.elements.size();
    if (Value folded; ips::foldExpression(index, folded)) {
        if (const auto* k = std::get_if<i32>(&folded)) {
            const auto chosen = *k >= 0 && static_cast<std::size_t>(*k) + 1 < count
                ? static_cast<std::size_t>(*k) : count - 1;
            emitter_.emitConst(palette_.elements[chosen]);
            return palette_.type;
        }
    }
    emitter_.emitExpr(index);
    const auto indexTemp = emitter_.temporary(ips::ValueType::I32);
    const auto result = emitter_.temporary(palette_.type);
    emitter_.ins(Opcode::LocalSet, 0, indexTemp.index());
    select(indexTemp.index(), result.index());
    emitter_.ins(Opcode::LocalGet, 0, result.index());
    return palette_.type;
}

ips::ValueType PaletteLowerer::ramp(const ips::Expr& t) {
    emitter_.emitExpr(t);
    if (palette_.elements.size() == 1) {
        // Even a constant result must preserve helper, RNG and trap effects.
        emitter_.ins(Opcode::Drop);
        emitter_.emitConst(palette_.elements.front());
        return palette_.type;
    }
    const auto stop = emitter_.temporary(ips::ValueType::I32);
    const auto fraction = emitter_.temporary(ips::ValueType::F32);
    if (emitter_.plan_.typeEngine().typeOf(t) == ips::ValueType::I32)
        emitter_.ins(Opcode::I32ToF32);
    emitter_.emitConst(0.0f);
    emitter_.ins(Opcode::F32Max);
    emitter_.emitConst(1.0f);
    emitter_.ins(Opcode::F32Min);
    emitter_.emitConst(static_cast<f32>(palette_.elements.size() - 1));
    emitter_.ins(Opcode::F32Mul);
    emitter_.ins(Opcode::Dup);
    emitter_.ins(Opcode::F32ToI32);
    emitter_.ins(Opcode::LocalSet, 0, stop.index());
    emitter_.ins(Opcode::LocalGet, 0, stop.index());
    emitter_.ins(Opcode::I32ToF32);
    emitter_.ins(Opcode::F32Sub);
    emitter_.ins(Opcode::LocalSet, 0, fraction.index());
    const auto result = emitter_.temporary(palette_.type);
    select(stop.index(), result.index(), fraction.index());
    emitter_.ins(Opcode::LocalGet, 0, result.index());
    return palette_.type;
}

void PaletteLowerer::select(u16 index, u16 result, std::optional<u16> fraction) {
    emitter_.ins(Opcode::Block);
    for (std::size_t k = 0; k + 1 < palette_.elements.size(); ++k) {
        emitter_.emitConst(static_cast<i32>(k));
        emitter_.ins(Opcode::LocalGet, 0, index);
        emitter_.ins(Opcode::I32Eq);
        emitter_.ins(Opcode::If);
        emitter_.emitConst(palette_.elements[k]);
        if (fraction) {
            emitter_.emitConst(palette_.elements[k + 1]);
            emitter_.ins(Opcode::LocalGet, 0, *fraction);
            emitter_.ins(palette_.type == ips::ValueType::Color ? Opcode::ColorMix : Opcode::Lerp);
        }
        emitter_.ins(Opcode::LocalSet, 0, result);
        emitter_.ins(Opcode::Br, 0, 1);
        emitter_.ins(Opcode::End);
    }
    emitter_.emitConst(palette_.elements.back());
    emitter_.ins(Opcode::LocalSet, 0, result);
    emitter_.ins(Opcode::End);
}

} // namespace iprop::compiler
