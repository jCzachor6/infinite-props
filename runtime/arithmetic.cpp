#include "iprop/VmInternals.hpp"

namespace iprop::detail {

// Integer arithmetic (spec §9.3). Phase 8 ships the minimal subset needed to
// exercise control flow (add, sub, eq, le_s); Phase 9 completes the group.
// All arithmetic wraps in u32 (spec §13.1: two's-complement wrapping).
bool stepArithmetic(Execution& ex, Instruction ins) {
    switch (ins.opcode) {
    case Opcode::I32Add: {
        const i32 b = ex.popI32();
        const i32 a = ex.popI32();
        ex.push(Value(static_cast<i32>(static_cast<u32>(a) + static_cast<u32>(b))));
        ++ex.frame().ip;
        return true;
    }
    case Opcode::I32Sub: {
        const i32 b = ex.popI32();
        const i32 a = ex.popI32();
        ex.push(Value(static_cast<i32>(static_cast<u32>(a) - static_cast<u32>(b))));
        ++ex.frame().ip;
        return true;
    }
    case Opcode::I32Eq: {
        const i32 b = ex.popI32();
        const i32 a = ex.popI32();
        ex.push(Value(static_cast<i32>(a == b ? 1 : 0)));
        ++ex.frame().ip;
        return true;
    }
    case Opcode::I32LeS: {
        const i32 b = ex.popI32();
        const i32 a = ex.popI32();
        ex.push(Value(static_cast<i32>(a <= b ? 1 : 0)));
        ++ex.frame().ip;
        return true;
    }
    default:
        return false;
    }
}

} // namespace iprop::detail
