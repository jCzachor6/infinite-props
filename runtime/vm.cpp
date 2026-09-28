#include "iprop/VM.hpp"

#include "iprop/VmInternals.hpp"

#include <utility>

namespace iprop {
namespace detail {

bool dispatch(Execution& ex, Instruction ins) {
    if (stepCore(ex, ins)) {
        return true;
    }
    if (stepStack(ex, ins)) {
        return true;
    }
    if (stepControl(ex, ins)) {
        return true;
    }
    if (stepArithmetic(ex, ins)) {
        return true;
    }
    return false; // remaining groups arrive with their phases
}

// Frame-lifecycle instructions: nop, call, return (spec §9.1).
bool stepCore(Execution& ex, Instruction ins) {
    switch (ins.opcode) {
    case Opcode::Nop:
        ++ex.frame().ip;
        return true;
    case Opcode::Call:
        ++ex.frame().ip; // the caller resumes after the call instruction
        ex.callFunction(ins.operand);
        return true;
    case Opcode::Return:
        ex.leaveFrame();
        return true;
    default:
        return false;
    }
}

Value Execution::pop() {
    Frame& frame = frames.back();
    if (frame.stack.empty()) {
        trap("stack underflow");
    }
    Value value = std::move(frame.stack.back());
    frame.stack.pop_back();
    return value;
}

Value Execution::peek() {
    Frame& frame = frames.back();
    if (frame.stack.empty()) {
        trap("stack underflow");
    }
    return frame.stack.back();
}

i32 Execution::popI32() {
    const Value value = pop();
    if (!std::holds_alternative<i32>(value)) {
        trap("stack type mismatch: expected i32");
    }
    return std::get<i32>(value);
}

f32 Execution::popF32() {
    const Value value = pop();
    if (!std::holds_alternative<f32>(value)) {
        trap("stack type mismatch: expected f32");
    }
    return std::get<f32>(value);
}

Color Execution::popColor() {
    const Value value = pop();
    if (!std::holds_alternative<Color>(value)) {
        trap("stack type mismatch: expected color");
    }
    return std::get<Color>(value);
}

void Execution::push(Value value) {
    Frame& frame = frames.back();
    if (frame.stack.size() >= kMaxStackDepth) {
        trap("operand stack depth exceeds limit");
    }
    frame.stack.push_back(std::move(value));
}

void Execution::spend(u64 cost) {
    if (cost > fuel) {
        trap("fuel exhausted");
    }
    fuel -= cost;
}

void Execution::callFunction(u16 functionIndex) {
    if (frames.size() >= kMaxCallDepth) {
        trap("call depth exceeds limit");
    }
    const auto& callee = file.code.functions[functionIndex];
    Frame& caller = frames.back();

    // Pop parameters in reverse: the last parameter is on top.
    std::vector<Value> args(callee.params.size());
    for (std::size_t p = callee.params.size(); p > 0; --p) {
        args[p - 1] = pop();
    }

    Frame next;
    next.functionIndex = functionIndex;
    next.locals = std::move(args);
    for (const ValueType type : callee.locals) {
        next.locals.push_back(type == ValueType::I32    ? Value(i32{0})
                              : type == ValueType::F32  ? Value(f32{0.f})
                                                        : Value(Color{}));
    }
    next.stream = caller.stream; // spec §8.2: call frames share the stream
    frames.push_back(std::move(next));
}

void Execution::leaveFrame() {
    Frame finished = std::move(frames.back());
    frames.pop_back();
    if (frames.empty()) {
        return; // entry finished; results are discarded
    }
    const auto& fn = file.code.functions[finished.functionIndex];
    // The validated function leaves exactly its results on the stack, in
    // declared order (deepest first). Defend against unvalidated input.
    if (finished.stack.size() != fn.results.size()) {
        trap("stack does not match declared results");
    }
    Frame& caller = frames.back();
    for (Value& value : finished.stack) {
        caller.stack.push_back(std::move(value));
    }
}

void Execution::ensureControl(u16 functionIndex) {
    if (functionIndex >= control.size()) {
        control.resize(file.code.functions.size());
    }
    if (!control[functionIndex].empty()) {
        return;
    }
    const auto& fn = file.code.functions[functionIndex];
    const auto count = fn.code.size() / 4;
    auto& info = control[functionIndex];
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

[[noreturn]] void Execution::trap(std::string message) const {
    std::size_t offset = 0;
    if (!frames.empty()) {
        const auto& fn = file.code.functions[frames.back().functionIndex];
        offset = fn.fileOffset + frames.back().ip * 4;
    }
    throw Trap(std::move(message), offset);
}

} // namespace detail

namespace {

// Zero value for a declared local type (spec §8.2: locals start zeroed).
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

struct VM::Impl {
    detail::Execution ex;

    explicit Impl(const LoadedFile& file) : ex(file) {}
};

VM::VM(const LoadedFile& file, const GenerationInputs& inputs)
    : impl_(std::make_unique<Impl>(file)) {
    impl_->ex.budget = file.meta.fuel;
    impl_->ex.fuel = file.meta.fuel;

    if (inputs.variant >= file.meta.variants) {
        throw Trap("variant index out of range", 0);
    }
    if (inputs.archetype >= file.meta.archetypes) {
        throw Trap("archetype index out of range", 0);
    }

    // Target canvas and scale (spec §8.1).
    const u32 targetW = inputs.sizeX != 0 ? inputs.sizeX : file.meta.sizeX;
    const u32 targetH = inputs.sizeY != 0 ? inputs.sizeY : file.meta.sizeY;
    impl_->ex.canvas.resize(targetW, targetH);
    impl_->ex.canvas.scaleX = static_cast<f32>(targetW) / static_cast<f32>(file.meta.sizeX);
    impl_->ex.canvas.scaleY = static_cast<f32>(targetH) / static_cast<f32>(file.meta.sizeY);

    // Root frame: (i32 variant, i32 archetype), then root uniforms (§6.1).
    detail::Frame root;
    root.functionIndex = 0;
    const auto& entry = file.code.entry();
    root.locals.reserve(entry.params.size() + entry.locals.size());
    root.locals.push_back(Value(static_cast<i32>(inputs.variant)));
    root.locals.push_back(Value(static_cast<i32>(inputs.archetype)));
    for (const auto& decl : file.meta.inputs) {
        const auto& name = file.pool.strings[decl.nameIndex];
        const auto found = inputs.uniforms.find(name);
        if (found == inputs.uniforms.end()) {
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
    root.stream = inputs.seed;
    impl_->ex.frames.push_back(std::move(root));
}

VM::~VM() = default;
VM::VM(VM&&) noexcept = default;
VM& VM::operator=(VM&&) noexcept = default;

void VM::run() {
    detail::Execution& ex = impl_->ex;
    while (!ex.frames.empty()) {
        detail::Frame& frame = ex.frames.back();
        const auto& fn = ex.file.code.functions[frame.functionIndex];
        if (frame.ip >= fn.code.size() / 4) {
            // Implicit return: the validated function ends with its declared
            // results on the stack.
            ex.leaveFrame();
            continue;
        }
        const auto ins = decodeInstruction(fn.code.subspan(frame.ip * 4, 4));
        ex.spend(1 + visitCost(ins));
        if (!detail::dispatch(ex, ins)) {
            ex.trap("opcode not implemented yet (interpreter phase pending)");
        }
    }
}

const Canvas& VM::canvas() const { return impl_->ex.canvas; }
u64 VM::fuel() const { return impl_->ex.fuel; }
u64 VM::fuelBudget() const { return impl_->ex.budget; }

} // namespace iprop
