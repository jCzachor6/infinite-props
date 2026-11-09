#include "iprop/VmInternals.hpp"

namespace iprop::detail {

// Stack and constant instructions (spec §9.2). Returns true when handled.
bool stepStack(Execution& ex, Instruction ins) {
    Frame& frame = ex.frames.back();
    switch (ins.opcode) {
    case Opcode::ConstI32:
        if (ins.operand >= ex.file.pool.ints.size()) {
            ex.trap("i32 constant index out of range", ErrorReason::IndexRange);
        }
        ex.push(ex.file.pool.ints[ins.operand]);
        break;
    case Opcode::ConstF32:
        if (ins.operand >= ex.file.pool.floats.size()) {
            ex.trap("f32 constant index out of range", ErrorReason::IndexRange);
        }
        ex.push(ex.file.pool.floats[ins.operand]);
        break;
    case Opcode::ConstColor:
        if (ins.operand >= ex.file.pool.colors.size()) {
            ex.trap("color constant index out of range", ErrorReason::IndexRange);
        }
        ex.push(ex.file.pool.colors[ins.operand]);
        break;

    case Opcode::LocalGet: {
        if (ins.operand >= frame.locals.size()) {
            ex.trap("local index out of range", ErrorReason::IndexRange);
        }
        const Value& local = frame.locals[ins.operand];
        // Plain scalars copy without visiting the variant.
        if (const auto* i = std::get_if<i32>(&local)) { ex.push(*i); break; }
        if (const auto* f = std::get_if<f32>(&local)) { ex.push(*f); break; }
        if (const auto* c = std::get_if<Color>(&local)) { ex.push(*c); break; }
        if (const auto* v = std::get_if<Vec2>(&local)) { ex.push(*v); break; }
        if (valueType(local).isGeometry()) {
            const bool assigned = std::visit([](const auto& value) {
                if constexpr (requires { value.data; }) return static_cast<bool>(value.data);
                return false;
            }, local);
            if (!assigned) ex.trap(std::string(valueTypeName(valueType(local))) +
                " is unassigned", ErrorReason::RuntimeViolation);
        }
        ex.push(local);
        break;
    }
    case Opcode::LocalSet: {
        if (ins.operand >= frame.locals.size()) {
            ex.trap("local index out of range", ErrorReason::IndexRange);
        }
        if (frame.stack.empty()) ex.trap("stack underflow", ErrorReason::StackUnderflow);
        Value& top = frame.stack.back();
        Value& local = frame.locals[ins.operand];
        if (const auto* i = std::get_if<i32>(&top)) local = *i;
        else if (const auto* f = std::get_if<f32>(&top)) local = *f;
        else if (const auto* c = std::get_if<Color>(&top)) local = *c;
        else if (const auto* v = std::get_if<Vec2>(&top)) local = *v;
        else local = std::move(top);
        frame.stack.pop_back();
        break;
    }
    case Opcode::LocalTee: {
        if (ins.operand >= frame.locals.size()) {
            ex.trap("local index out of range", ErrorReason::IndexRange);
        }
        if (frame.stack.empty()) ex.trap("stack underflow", ErrorReason::StackUnderflow);
        const Value& top = frame.stack.back();
        Value& local = frame.locals[ins.operand];
        if (const auto* i = std::get_if<i32>(&top)) local = *i;
        else if (const auto* f = std::get_if<f32>(&top)) local = *f;
        else if (const auto* c = std::get_if<Color>(&top)) local = *c;
        else if (const auto* v = std::get_if<Vec2>(&top)) local = *v;
        else local = top;
        break;
    }
    case Opcode::Drop:
        if (frame.stack.empty()) ex.trap("stack underflow", ErrorReason::StackUnderflow);
        frame.stack.pop_back();
        break;
    case Opcode::Dup: {
        if (frame.stack.empty()) ex.trap("stack underflow", ErrorReason::StackUnderflow);
        // Copy first: the push may move the stack's storage.
        const Value& top = frame.stack.back();
        if (const auto* i = std::get_if<i32>(&top)) ex.push(i32{*i});
        else if (const auto* f = std::get_if<f32>(&top)) ex.push(f32{*f});
        else if (const auto* c = std::get_if<Color>(&top)) ex.push(Color{*c});
        else if (const auto* v = std::get_if<Vec2>(&top)) ex.push(Vec2{*v});
        else ex.push(ex.peek());
        break;
    }

    default:
        return false;
    }
    ++frame.ip;
    return true;
}

} // namespace iprop::detail
