#pragma once

// The three instruction groups a generated program spends its instructions
// in: stack and constants, structured control flow and scalar arithmetic.
// They are defined inline so the interpreter loop reaches them through one
// jump table, without a call; every other group stays in its own source file.
// Included at the end of VmInternals.hpp, which declares Execution.

#include "iprop/math/Scalar.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace iprop::detail {

// Stack and constant instructions (spec §9.2). Returns true when handled.
IPROP_ALWAYS_INLINE bool stepStack(Execution& ex, Instruction ins) {
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
        if (frame.stack.empty()) ex.stackUnderflow();
        moveValue(frame.locals[ins.operand], frame.stack.back());
        frame.stack.pop_back();
        break;
    }
    case Opcode::LocalTee: {
        if (ins.operand >= frame.locals.size()) {
            ex.trap("local index out of range", ErrorReason::IndexRange);
        }
        if (frame.stack.empty()) ex.stackUnderflow();
        copyValue(frame.locals[ins.operand], frame.stack.back());
        break;
    }
    case Opcode::Drop:
        if (frame.stack.empty()) ex.stackUnderflow();
        frame.stack.pop_back();
        break;
    case Opcode::Dup: {
        if (frame.stack.empty()) ex.stackUnderflow();
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


// Shared branch mechanics: the target label stays active (its `end` pops
// it); inner labels unwind and the stack restores the target's entry height.
IPROP_ALWAYS_INLINE void branch(Execution& ex, Frame& frame, u16 depth) {
    if (depth >= frame.labels.size()) {
        ex.trap("branch depth out of range", ErrorReason::InvalidControl);
    }
    const Label target = frame.labels[frame.labels.size() - 1 - depth];
    ex.closeLayers(frame.labels.size() - depth);
    frame.labels.resize(frame.labels.size() - depth);
    frame.stack.resize(target.stackHeight);
    frame.ip = target.kind == 1 ? target.cont : target.end;
}


// Structured control flow (spec §9.1), with WASM branch semantics.
IPROP_ALWAYS_INLINE bool stepControl(Execution& ex, Instruction ins) {
    Frame& frame = ex.frames.back();
    switch (ins.opcode) {
    case Opcode::Block:
    case Opcode::Loop:
    case Opcode::LayerBegin:
    case Opcode::ClipBegin: {
        ex.ensureControl(frame.functionIndex);
        const auto& info = ex.control[frame.functionIndex][frame.ip];
        Label label;
        if (ins.opcode == Opcode::LayerBegin) ex.beginLayer(ins.operand, ins.aux);
        if (ins.opcode == Opcode::ClipBegin) ex.beginClip();
        label.kind = ins.opcode == Opcode::Loop ? 1u : ins.opcode == Opcode::LayerBegin ? 3u :
            ins.opcode == Opcode::ClipBegin ? 4u : 0u;
        label.cont = ins.opcode == Opcode::Loop ? frame.ip + 1 : info.endIndex;
        label.end = info.endIndex;
        label.stackHeight = frame.stack.size();
        frame.labels.push_back(label);
        ++frame.ip;
        return true;
    }

    case Opcode::If: {
        ex.ensureControl(frame.functionIndex);
        const auto& info = ex.control[frame.functionIndex][frame.ip];
        const i32 condition = ex.popI32();
        Label label;
        label.kind = 2u;
        label.cont = info.endIndex;
        label.end = info.endIndex;
        label.stackHeight = frame.stack.size(); // after popping the condition
        frame.labels.push_back(label);
        if (condition != 0) {
            ++frame.ip;
        } else if (info.elseIndex != ControlInfo::kNone) {
            frame.ip = info.elseIndex + 1;
        } else {
            frame.ip = info.endIndex; // the matching `end` pops the label
        }
        return true;
    }

    case Opcode::Else: {
        if (frame.labels.empty()) {
            ex.trap("else without if", ErrorReason::InvalidControl);
        }
        // Reached by falling through the then-branch: restore the entry
        // height and skip the else body; the `end` pops the label.
        const Label label = frame.labels.back();
        frame.stack.resize(label.stackHeight);
        frame.ip = label.end;
        return true;
    }

    case Opcode::End: {
        if (frame.labels.empty()) {
            ex.trap("end without block", ErrorReason::InvalidControl);
        }
        const Label label = frame.labels.back();
        frame.labels.pop_back();
        ex.closeLayers(frame.labels.size());
        frame.stack.resize(label.stackHeight);
        ++frame.ip;
        return true;
    }

    case Opcode::Br: {
        branch(ex, frame, ins.operand);
        return true;
    }

    case Opcode::BrIf: {
        const i32 condition = ex.popI32();
        if (condition != 0) {
            branch(ex, frame, ins.operand);
        } else {
            ++frame.ip;
        }
        return true;
    }

    default:
        return false;
    }
}

// Shared shapes for the regular two-operand cases: pop, compute, push,
// advance. Special cases (division, shifts, conversions) stay inline.
template<class Operation> IPROP_ALWAYS_INLINE void i32Binop(Execution& ex, Operation fn) {
    const u32 b = static_cast<u32>(ex.popI32());
    const u32 a = static_cast<u32>(ex.popI32());
    ex.push(static_cast<i32>(fn(a, b)));
    ++ex.frame().ip;
}

template<class Operation> IPROP_ALWAYS_INLINE void i32Compare(Execution& ex, Operation fn) {
    const i32 b = ex.popI32();
    const i32 a = ex.popI32();
    ex.push(fn(a, b));
    ++ex.frame().ip;
}

template<class Operation> IPROP_ALWAYS_INLINE void f32Binop(Execution& ex, Operation fn) {
    const f32 b = ex.popF32();
    const f32 a = ex.popF32();
    ex.push(fn(a, b));
    ++ex.frame().ip;
}

template<class Operation> IPROP_ALWAYS_INLINE void f32Compare(Execution& ex, Operation fn) {
    const f32 b = ex.popF32();
    const f32 a = ex.popF32();
    ex.push(fn(a, b));
    ++ex.frame().ip;
}



IPROP_ALWAYS_INLINE bool stepArithmetic(Execution& ex, Instruction ins) {
    switch (ins.opcode) {
    case Opcode::I32Min:
        i32Compare(ex, [](i32 a, i32 b) { return std::min(a, b); });
        return true;
    case Opcode::I32Max:
        i32Compare(ex, [](i32 a, i32 b) { return std::max(a, b); });
        return true;
    case Opcode::I32Clamp: {
        const i32 b = ex.popI32();
        const i32 a = ex.popI32();
        const i32 x = ex.popI32();
        ex.push(std::clamp(x, std::min(a, b), std::max(a, b)));
        ++ex.frame().ip;
        return true;
    }
    case Opcode::I32Add:
        i32Binop(ex, [](u32 a, u32 b) { return a + b; });
        return true;
    case Opcode::I32Sub:
        i32Binop(ex, [](u32 a, u32 b) { return a - b; });
        return true;
    case Opcode::I32Mul:
        i32Binop(ex, [](u32 a, u32 b) { return a * b; });
        return true;
    case Opcode::I32DivS: {
        const i32 b = ex.popI32();
        const i32 a = ex.popI32();
        if (b == 0) ex.trap("divide by zero", ErrorReason::DivideByZero);
        if (a == std::numeric_limits<i32>::min() && b == -1) {
            ex.trap("integer overflow", ErrorReason::IntegerOverflow);
        }
        ex.push(a / b);
        ++ex.frame().ip;
        return true;
    }
    case Opcode::I32DivU: {
        const u32 b = static_cast<u32>(ex.popI32());
        const u32 a = static_cast<u32>(ex.popI32());
        if (b == 0) ex.trap("divide by zero", ErrorReason::DivideByZero);
        ex.push(static_cast<i32>(a / b));
        ++ex.frame().ip;
        return true;
    }
    case Opcode::I32RemS: {
        const i32 b = ex.popI32();
        const i32 a = ex.popI32();
        if (b == 0) ex.trap("divide by zero", ErrorReason::DivideByZero);
        if (a == std::numeric_limits<i32>::min() && b == -1) {
            ex.push(i32{0});
        } else {
            ex.push(a % b);
        }
        ++ex.frame().ip;
        return true;
    }
    case Opcode::I32RemU: {
        const u32 b = static_cast<u32>(ex.popI32());
        const u32 a = static_cast<u32>(ex.popI32());
        if (b == 0) ex.trap("divide by zero", ErrorReason::DivideByZero);
        ex.push(static_cast<i32>(a % b));
        ++ex.frame().ip;
        return true;
    }
    case Opcode::I32And: {
        const i32 b = ex.popI32();
        const i32 a = ex.popI32();
        ex.push(static_cast<i32>(static_cast<u32>(a) & static_cast<u32>(b)));
        ++ex.frame().ip;
        return true;
    }
    case Opcode::I32Or: {
        const i32 b = ex.popI32();
        const i32 a = ex.popI32();
        ex.push(static_cast<i32>(static_cast<u32>(a) | static_cast<u32>(b)));
        ++ex.frame().ip;
        return true;
    }
    case Opcode::I32Xor: {
        const i32 b = ex.popI32();
        const i32 a = ex.popI32();
        ex.push(static_cast<i32>(static_cast<u32>(a) ^ static_cast<u32>(b)));
        ++ex.frame().ip;
        return true;
    }
    case Opcode::I32Shl: {
        const u32 shift = static_cast<u32>(ex.popI32()) & 31u;
        const i32 a = ex.popI32();
        ex.push(static_cast<i32>(static_cast<u32>(a) << shift));
        ++ex.frame().ip;
        return true;
    }
    case Opcode::I32ShrS: {
        const u32 shift = static_cast<u32>(ex.popI32()) & 31u;
        const i32 a = ex.popI32();
        ex.push(math::shiftRightSigned(a, shift));
        ++ex.frame().ip;
        return true;
    }
    case Opcode::I32ShrU: {
        const u32 shift = static_cast<u32>(ex.popI32()) & 31u;
        const i32 a = ex.popI32();
        ex.push(static_cast<i32>(static_cast<u32>(a) >> shift));
        ++ex.frame().ip;
        return true;
    }
    case Opcode::I32Neg: {
        const i32 a = ex.popI32();
        ex.push(static_cast<i32>(0u - static_cast<u32>(a)));
        ++ex.frame().ip;
        return true;
    }
    case Opcode::I32Not: {
        const i32 a = ex.popI32();
        ex.push(static_cast<i32>(~static_cast<u32>(a)));
        ++ex.frame().ip;
        return true;
    }

    case Opcode::I32Eq:
        i32Compare(ex, [](i32 a, i32 b) { return a == b ? 1 : 0; });
        return true;
    case Opcode::I32Ne:
        i32Compare(ex, [](i32 a, i32 b) { return a != b ? 1 : 0; });
        return true;
    case Opcode::I32LtS:
        i32Compare(ex, [](i32 a, i32 b) { return a < b ? 1 : 0; });
        return true;
    case Opcode::I32LtU:
        i32Compare(ex, [](i32 a, i32 b) {
            return static_cast<u32>(a) < static_cast<u32>(b) ? 1 : 0;
        });
        return true;
    case Opcode::I32GtS:
        i32Compare(ex, [](i32 a, i32 b) { return a > b ? 1 : 0; });
        return true;
    case Opcode::I32GtU:
        i32Compare(ex, [](i32 a, i32 b) {
            return static_cast<u32>(a) > static_cast<u32>(b) ? 1 : 0;
        });
        return true;
    case Opcode::I32LeS:
        i32Compare(ex, [](i32 a, i32 b) { return a <= b ? 1 : 0; });
        return true;
    case Opcode::I32LeU:
        i32Compare(ex, [](i32 a, i32 b) {
            return static_cast<u32>(a) <= static_cast<u32>(b) ? 1 : 0;
        });
        return true;
    case Opcode::I32GeS:
        i32Compare(ex, [](i32 a, i32 b) { return a >= b ? 1 : 0; });
        return true;
    case Opcode::I32GeU:
        i32Compare(ex, [](i32 a, i32 b) {
            return static_cast<u32>(a) >= static_cast<u32>(b) ? 1 : 0;
        });
        return true;

    case Opcode::F32Add:
        f32Binop(ex, [](f32 a, f32 b) { return a + b; });
        return true;
    case Opcode::F32Sub:
        f32Binop(ex, [](f32 a, f32 b) { return a - b; });
        return true;
    case Opcode::F32Mul:
        f32Binop(ex, [](f32 a, f32 b) { return a * b; });
        return true;
    case Opcode::F32Div:
        f32Binop(ex, [](f32 a, f32 b) { return a / b; }); // IEEE: x/0 = inf
        return true;
    case Opcode::F32Eq:
        f32Compare(ex, [](f32 a, f32 b) { return a == b ? 1 : 0; }); // NaN: false
        return true;
    case Opcode::F32Ne:
        f32Compare(ex, [](f32 a, f32 b) { return a != b ? 1 : 0; }); // NaN: true
        return true;
    case Opcode::F32Lt:
        f32Compare(ex, [](f32 a, f32 b) { return a < b ? 1 : 0; });
        return true;
    case Opcode::F32Gt:
        f32Compare(ex, [](f32 a, f32 b) { return a > b ? 1 : 0; });
        return true;
    case Opcode::F32Le:
        f32Compare(ex, [](f32 a, f32 b) { return a <= b ? 1 : 0; });
        return true;
    case Opcode::F32Ge:
        f32Compare(ex, [](f32 a, f32 b) { return a >= b ? 1 : 0; });
        return true;
    case Opcode::F32Neg: {
        ex.push(-ex.popF32());
        ++ex.frame().ip;
        return true;
    }
    case Opcode::F32Abs: {
        ex.push(std::fabs(ex.popF32()));
        ++ex.frame().ip;
        return true;
    }
    case Opcode::F32Sqrt: {
        ex.push(std::sqrt(ex.popF32()));
        ++ex.frame().ip;
        return true;
    }
    case Opcode::F32Sin: {
        ex.push(math::f32Sin(ex.popF32()));
        ++ex.frame().ip;
        return true;
    }
    case Opcode::F32Cos: {
        ex.push(math::f32Cos(ex.popF32()));
        ++ex.frame().ip;
        return true;
    }
    case Opcode::F32Floor: {
        ex.push(static_cast<f32>(std::floor(ex.popF32())));
        ++ex.frame().ip;
        return true;
    }
    case Opcode::F32Ceil: {
        ex.push(static_cast<f32>(std::ceil(ex.popF32())));
        ++ex.frame().ip;
        return true;
    }
    case Opcode::F32Min: {
        const f32 b = ex.popF32();
        const f32 a = ex.popF32();
        ex.push(math::f32MinMax(a, b, false));
        ++ex.frame().ip;
        return true;
    }
    case Opcode::F32Max: {
        const f32 b = ex.popF32();
        const f32 a = ex.popF32();
        ex.push(math::f32MinMax(a, b, true));
        ++ex.frame().ip;
        return true;
    }
    case Opcode::F32Pow: {
        const f32 b = ex.popF32();
        const f32 a = ex.popF32();
        ex.push(math::f32Pow(a, b));
        ++ex.frame().ip;
        return true;
    }
    case Opcode::F32Exp: {
        ex.push(math::f32Exp(ex.popF32()));
        ++ex.frame().ip;
        return true;
    }
    case Opcode::F32Atan2: {
        const f32 b = ex.popF32(); // x
        const f32 a = ex.popF32(); // y
        ex.push(math::f32Atan2(a, b));
        ++ex.frame().ip;
        return true;
    }
    case Opcode::I32ToF32: {
        ex.push(static_cast<f32>(ex.popI32()));
        ++ex.frame().ip;
        return true;
    }
    case Opcode::F32ToI32: {
        ex.push(math::f32ToI32(ex.popF32()));
        ++ex.frame().ip;
        return true;
    }


    default: return false;
    }
}

} // namespace iprop::detail
