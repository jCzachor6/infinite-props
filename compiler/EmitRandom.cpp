#include "Emit.hpp"

namespace iprop::compiler {

ips::ValueType Emitter::emitSelection(const SelectionCall& call) {
    std::optional<TemporaryLocals::Lease> seed;
    if (call.seed) {
        emitArg(*call.seed);
        seed.emplace(temporary(ips::ValueType::I32));
        ins(Opcode::LocalSet, 0, seed->index());
    }
    // Calls eagerly evaluate all choices once, in signature order, before the
    // selection draw. Leases remain live across nested choice expressions.
    std::vector<TemporaryLocals::Lease> values;
    for (const auto& choice : call.choices) {
        emitArg(choice);
        values.push_back(temporary(call.result));
        ins(Opcode::LocalSet, 0, values.back().index());
    }
    const auto index = temporary(ips::ValueType::I32);
    emitConst(static_cast<i32>(values.size()));
    if (seed) {
        ins(Opcode::LocalGet, 0, seed->index());
        ins(Opcode::StreamPush);
    }
    ins(Opcode::RngIndex);
    if (seed) ins(Opcode::StreamPop);
    ins(Opcode::LocalSet, 0, index.index());
    const auto result = temporary(call.result);
    const auto select = [&](auto&& self, std::size_t begin, std::size_t end) -> void {
        if (end - begin == 1) {
            ins(Opcode::LocalGet, 0, values[begin].index());
            ins(Opcode::LocalSet, 0, result.index());
            return;
        }
        const auto middle = begin + (end - begin) / 2;
        ins(Opcode::LocalGet, 0, index.index());
        emitConst(static_cast<i32>(middle));
        ins(Opcode::I32LtS);
        ins(Opcode::If);
        self(self, begin, middle);
        ins(Opcode::Else);
        self(self, middle, end);
        ins(Opcode::End);
    };
    select(select, 0, values.size());
    ins(Opcode::LocalGet, 0, result.index());
    return call.result;
}

} // namespace iprop::compiler
