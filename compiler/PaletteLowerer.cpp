#include "PaletteLowerer.hpp"
#include "Emit.hpp"
#include "ips/Fold.hpp"

namespace iprop::compiler {

ips::ValueType PaletteLowerer::index(const ips::Expr& index) {
    const std::size_t count = palette_.elements.size();
    if (const auto& constant = emitter_.current_->expression(index).constant; constant) {
        if (const auto* k = std::get_if<i32>(&*constant)) {
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

ips::ValueType PaletteLowerer::ramp(const PlannedArgument& t) {
    emitter_.emitExpr(*t.expression);
    if (palette_.elements.size() == 1) {
        // Even a constant result must preserve helper, RNG and trap effects.
        emitter_.ins(Opcode::Drop);
        emitter_.emitConst(palette_.elements.front());
        return palette_.type;
    }
    const auto stop = emitter_.temporary(ips::ValueType::I32);
    const auto fraction = emitter_.temporary(ips::ValueType::F32);
    if (t.widen)
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
    const auto tree = [&](auto&& self, std::size_t first, std::size_t end) -> void {
        if (end - first > 1) {
            const auto middle = first + (end - first) / 2;
            emitter_.ins(Opcode::LocalGet, 0, index);
            emitter_.emitConst(static_cast<i32>(middle));
            // Unsigned comparison sends negative indices to the default
            // last leaf, just like indices past the end of the palette.
            emitter_.ins(Opcode::I32LtU);
            emitter_.ins(Opcode::If);
            self(self, first, middle);
            emitter_.ins(Opcode::Else);
            self(self, middle, end);
            emitter_.ins(Opcode::End);
            return;
        }
        emitter_.emitConst(palette_.elements[first]);
        if (fraction && first + 1 < palette_.elements.size()) {
            emitter_.emitConst(palette_.elements[first + 1]);
            emitter_.ins(Opcode::LocalGet, 0, *fraction);
            emitter_.ins(palette_.type == ips::ValueType::Color ? Opcode::ColorMix : Opcode::Lerp);
        }
        emitter_.ins(Opcode::LocalSet, 0, result);
    };
    tree(tree, 0, palette_.elements.size());
}

} // namespace iprop::compiler
