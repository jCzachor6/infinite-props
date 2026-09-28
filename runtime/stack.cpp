#include "iprop/VmInternals.hpp"

namespace iprop::detail {

// Stack and constant instructions (spec §9.2). Returns true when handled.
bool stepStack(Execution& ex, Instruction ins) {
    Frame& frame = ex.frames.back();
    switch (ins.opcode) {
    case Opcode::ConstI32:
        ex.push(Value(ex.file.pool.ints[ins.operand]));
        break;
    case Opcode::ConstF32:
        ex.push(Value(ex.file.pool.floats[ins.operand]));
        break;
    case Opcode::ConstColor:
        ex.push(Value(ex.file.pool.colors[ins.operand]));
        break;

    case Opcode::LocalGet:
        ex.push(frame.locals[ins.operand]);
        break;
    case Opcode::LocalSet:
        frame.locals[ins.operand] = ex.pop();
        break;
    case Opcode::LocalTee:
        frame.locals[ins.operand] = ex.peek();
        break;
    case Opcode::Drop:
        (void)ex.pop();
        break;
    case Opcode::Dup:
        ex.push(ex.peek());
        break;

    default:
        return false;
    }
    ++frame.ip;
    return true;
}

} // namespace iprop::detail
