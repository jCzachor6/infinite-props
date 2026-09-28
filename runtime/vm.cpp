#include "iprop/VM.hpp"

#include <limits>
#include <utility>

namespace iprop {
namespace {

// Zero value for a declared local type.
Value zeroValue(ValueType type) {
    switch (type) {
    case ValueType::I32: return Value(i32{0});
    case ValueType::F32: return Value(f32{0.f});
    case ValueType::Color: return Value(Color{});
    }
    return Value(i32{0});
}

// Per-instruction visit cost of composite operations (spec §8.3/§11). Canvas
// operations fill this in during Phase 11; everything else costs 1 total.
u64 visitCost(Instruction) {
    return 0;
}

} // namespace

VM::VM(const LoadedFile& file, const GenerationInputs& inputs)
    : file_(file), inputs_(inputs) {
    budget_ = file.meta.fuel;
    fuel_ = budget_;
}

void VM::run() {
    if (inputs_.variant >= file_.meta.variants) {
        throw Trap("variant index out of range", 0);
    }
    if (inputs_.archetype >= file_.meta.archetypes) {
        throw Trap("archetype index out of range", 0);
    }

    // Target canvas and scale (spec §8.1).
    const u32 targetW = inputs_.sizeX != 0 ? inputs_.sizeX : file_.meta.sizeX;
    const u32 targetH = inputs_.sizeY != 0 ? inputs_.sizeY : file_.meta.sizeY;
    canvas_.resize(targetW, targetH);
    canvas_.scaleX = static_cast<f32>(targetW) / static_cast<f32>(file_.meta.sizeX);
    canvas_.scaleY = static_cast<f32>(targetH) / static_cast<f32>(file_.meta.sizeY);

    pushRootFrame();
    execute();
}

void VM::pushRootFrame() {
    const auto& entry = file_.code.entry();
    Frame root;
    root.functionIndex = 0;
    root.locals.reserve(entry.params.size() + entry.locals.size());

    // Params: (i32 variant, i32 archetype), then root uniforms (spec §6.1).
    root.locals.push_back(Value(static_cast<i32>(inputs_.variant)));
    root.locals.push_back(Value(static_cast<i32>(inputs_.archetype)));
    for (const auto& decl : file_.meta.inputs) {
        const auto& name = file_.pool.strings[decl.nameIndex];
        const auto found = inputs_.uniforms.find(name);
        if (found == inputs_.uniforms.end()) {
            switch (decl.type) {
            case InputType::Color: root.locals.push_back(Value(decl.colorDefault)); break;
            case InputType::F32: root.locals.push_back(Value(decl.f32Default)); break;
            case InputType::U32:
                root.locals.push_back(Value(static_cast<i32>(decl.u32Default)));
                break;
            }
        } else {
            if (found->second.type != decl.type) {
                throw Trap("uniform override type mismatch for '" + name + "'", 0);
            }
            switch (decl.type) {
            case InputType::Color: root.locals.push_back(Value(found->second.color)); break;
            case InputType::F32: root.locals.push_back(Value(found->second.f)); break;
            case InputType::U32:
                root.locals.push_back(Value(static_cast<i32>(found->second.u)));
                break;
            }
        }
    }
    for (const ValueType type : entry.locals) {
        root.locals.push_back(zeroValue(type));
    }
    root.stream = inputs_.seed;
    frames_.push_back(std::move(root));
}

void VM::execute() {
    while (!frames_.empty()) {
        Frame& frame = frames_.back();
        const auto& fn = file_.code.functions[frame.functionIndex];
        if (frame.ip >= fn.code.size() / 4) {
            // Implicit return: the validated function ends with its declared
            // results on the stack.
            leaveFrame();
            continue;
        }
        const auto ins = decodeInstruction(fn.code.subspan(frame.ip * 4, 4));
        spend(1 + visitCost(ins));
        step(ins);
    }
}

void VM::ensureControl(u16 functionIndex) {
    if (functionIndex >= control_.size()) {
        control_.resize(file_.code.functions.size());
    }
    if (!control_[functionIndex].empty()) {
        return;
    }
    const auto& fn = file_.code.functions[functionIndex];
    const auto count = fn.code.size() / 4;
    auto& info = control_[functionIndex];
    info.assign(count, {});
    std::vector<std::size_t> open;
    for (std::size_t i = 0; i < count; ++i) {
        const auto ins = decodeInstruction(fn.code.subspan(i * 4, 4));
        switch (ins.opcode) {
        case Opcode::Block:
        case Opcode::Loop:
        case Opcode::If:
            open.push_back(i);
            break;
        case Opcode::Else: {
            const std::size_t opener = open.back();
            info[opener].elseIndex = i;
            break;
        }
        case Opcode::End: {
            const std::size_t opener = open.back();
            open.pop_back();
            info[opener].endIndex = i;
            break;
        }
        default: break;
        }
    }
}

void VM::step(Instruction ins) {
    Frame& frame = frames_.back();
    switch (ins.opcode) {
    case Opcode::Nop:
        ++frame.ip;
        return;

    case Opcode::ConstI32:
        push(Value(file_.pool.ints[ins.operand]));
        ++frame.ip;
        return;
    case Opcode::ConstF32:
        push(Value(file_.pool.floats[ins.operand]));
        ++frame.ip;
        return;
    case Opcode::ConstColor:
        push(Value(file_.pool.colors[ins.operand]));
        ++frame.ip;
        return;

    case Opcode::LocalGet:
        push(frame.locals[ins.operand]);
        ++frame.ip;
        return;
    case Opcode::LocalSet:
        frame.locals[ins.operand] = pop();
        ++frame.ip;
        return;
    case Opcode::LocalTee:
        frame.locals[ins.operand] = frame.stack.back();
        ++frame.ip;
        return;
    case Opcode::Drop:
        (void)pop();
        ++frame.ip;
        return;
    case Opcode::Dup:
        push(frame.stack.back());
        ++frame.ip;
        return;

    case Opcode::Call:
        ++frame.ip; // the caller resumes after the call instruction
        callFunction(ins.operand);
        return; // frame switch: the new frame's ip starts at 0

    case Opcode::Return:
        leaveFrame();
        return;

    default:
        trap("opcode not implemented yet (interpreter phase pending)");
    }
}

void VM::callFunction(u16 functionIndex) {
    const auto& callee = file_.code.functions[functionIndex];
    Frame& caller = frames_.back();

    // Pop parameters in reverse: the last parameter is on top.
    std::vector<Value> args(callee.params.size());
    for (std::size_t p = callee.params.size(); p > 0; --p) {
        args[p - 1] = pop();
    }

    Frame next;
    next.functionIndex = functionIndex;
    next.locals = std::move(args);
    for (const ValueType type : callee.locals) {
        next.locals.push_back(zeroValue(type));
    }
    next.stream = caller.stream; // spec §8.2: call frames share the stream
    frames_.push_back(std::move(next));
}

void VM::leaveFrame() {
    Frame finished = std::move(frames_.back());
    frames_.pop_back();
    if (frames_.empty()) {
        return; // entry finished; results are discarded
    }
    // The validated function left exactly its results on the stack, in
    // declared order (deepest first).
    Frame& caller = frames_.back();
    for (Value& value : finished.stack) {
        caller.stack.push_back(std::move(value));
    }
}

Value VM::pop() {
    Frame& frame = frames_.back();
    if (frame.stack.empty()) {
        trap("stack underflow");
    }
    Value value = std::move(frame.stack.back());
    frame.stack.pop_back();
    return value;
}

void VM::push(Value value) {
    Frame& frame = frames_.back();
    if (frame.stack.size() >= kMaxStackDepth) {
        trap("operand stack depth exceeds limit");
    }
    frame.stack.push_back(std::move(value));
}

void VM::spend(u64 cost) {
    if (cost > fuel_) {
        trap("fuel exhausted");
    }
    fuel_ -= cost;
}

[[noreturn]] void VM::trap(std::string message) const {
    std::size_t offset = 0;
    if (!frames_.empty()) {
        const auto& fn = file_.code.functions[frames_.back().functionIndex];
        offset = fn.fileOffset + frames_.back().ip * 4;
    }
    throw Trap(std::move(message), offset);
}

} // namespace iprop
