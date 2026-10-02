#include "iprop/VM.hpp"

#include "iprop/VmInternals.hpp"

#include <cmath>
#include <utility>

namespace iprop {
namespace {

// Every frame starts each declared local with the same typed zero value.
Value zeroValue(ValueType type) {
    switch (type) {
    case ValueType::I32: return Value(i32{0});
    case ValueType::F32: return Value(f32{0.f});
    case ValueType::Color: return Value(Color{});
    case ValueType::Vec2: return Value(Vec2{});
    }
    return Value(i32{0});
}

} // namespace

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
    if (stepVector(ex, ins) || stepSdf(ex, ins) || stepColor(ex, ins) || stepCurves(ex, ins)) {
        return true;
    }
    if (stepSeeded(ex, ins)) {
        return true;
    }
    if (stepCanvas(ex, ins)) {
        return true;
    }
    if (stepOutputs(ex, ins)) {
        return true;
    }
    if (stepParts(ex, ins)) {
        return true;
    }
    return false; // everything is implemented once parts land (Phase 13)
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

Vec2 Execution::popVec2() {
    const Value value = pop();
    if (!std::holds_alternative<Vec2>(value)) {
        trap("stack type mismatch: expected vec2");
    }
    return std::get<Vec2>(value);
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
        next.locals.push_back(zeroValue(type));
    }
    next.stream = caller.stream; // spec §8.2: call frames share the stream
    next.shareStream = true;     // ...like a reference: state propagates back
    frames.push_back(std::move(next));
}

void Execution::callPart(u16 partIndex, u8 transform) {
    if (partIndex >= file.code.parts.size()) {
        trap("part index out of range");
    }
    if (frames.size() >= kMaxCallDepth) {
        trap("call depth exceeds limit");
    }
    const auto& part = file.code.parts[partIndex];

    // Stack order (spec §9.10): uniforms…, seed, variant, archetype, atX,
    // atY — the top is popped first.
    const f32 atY = popF32();
    const f32 atX = popF32();
    const i32 archetype = popI32();
    const i32 variant = popI32();
    const u32 seed = static_cast<u32>(popI32());
    const auto& fn = file.code.functions[part.functionIndex];
    // The part's params are (i32 variant, i32 archetype) + uniforms; the
    // variant/archetype come from the part.draw arguments.
    std::vector<Value> args(fn.params.size());
    args[0] = Value(variant);
    args[1] = Value(archetype);
    for (std::size_t p = fn.params.size(); p > 2; --p) {
        args[p - 1] = pop();
    }

    const auto d4 = D4Transform::fromKind(transform);
    if (!d4) trap("invalid part transform");
    PartRenderPlan render;
    try {
        render = PartRenderPlan::create(part, {canvas.scaleX, canvas.scaleY}, {atX, atY}, *d4);
    } catch (const std::invalid_argument& error) {
        trap(error.what());
    }
    spend(render.blitCost);
    PartCallState state;
    state.partIndex = partIndex;
    state.render = render;
    state.savedCanvas = std::move(canvas);
    partCalls.push_back(std::move(state));

    canvas.resize(static_cast<u32>(render.sourceExtent.x), static_cast<u32>(render.sourceExtent.y));
    canvas.scaleX = render.sourceScale.x;
    canvas.scaleY = render.sourceScale.y;

    Frame next;
    next.functionIndex = part.functionIndex;
    next.locals = std::move(args);
    for (const ValueType type : fn.locals) {
        next.locals.push_back(zeroValue(type));
    }
    next.stream = seed; // spec §8.2: a part frame starts its own stream
    next.isPart = true;
    ++frames.back().ip; // the caller resumes after the part.draw instruction
    frames.push_back(std::move(next));
}

void Execution::leaveFrame() {
    Frame finished = std::move(frames.back());
    frames.pop_back();
    if (frames.empty()) {
        // Entry finished; its results are discarded by design, so the stack
        // is captured as-is for tooling (debugStack) without a results check.
        entryStack = std::move(finished.stack);
        return;
    }
    const auto& fn = file.code.functions[finished.functionIndex];
    // The validated function leaves exactly its results on the stack, in
    // declared order (deepest first). Defend against unvalidated input.
    if (finished.stack.size() != fn.results.size()) {
        trap("stack does not match declared results");
    }
    Frame& caller = frames.back();
    if (finished.isPart) {
        // Restore the caller's canvas and blit the sub-canvas (spec §10.5).
        PartCallState state = std::move(partCalls.back());
        partCalls.pop_back();
        Canvas sub = std::move(canvas);
        canvas = std::move(state.savedCanvas);
        for (u32 sy = 0; sy < sub.height; ++sy) {
            for (u32 sx = 0; sx < sub.width; ++sx) {
                const Color& src = sub.pixels[static_cast<std::size_t>(sy) * sub.width + sx];
                if (src.a == 0) {
                    continue; // transparent sub-pixels are not blitted
                }
                const auto pixel = state.render.transform.mapPixel(
                    {static_cast<i32>(sx), static_cast<i32>(sy)}, state.render.sourceExtent);
                const i64 x = static_cast<i64>(state.render.targetOrigin.x) + pixel.x;
                const i64 y = static_cast<i64>(state.render.targetOrigin.y) + pixel.y;
                if (x >= 0 && y >= 0 && x < canvas.width && y < canvas.height) {
                    compositePixel(*this, static_cast<i32>(x), static_cast<i32>(y), src);
                }
            }
        }
        caller.lastPart = state.partIndex; // the part-output register
    } else if (finished.shareStream) {
        caller.stream = finished.stream; // reference semantics (spec §8.2)
    }
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
            if (open.empty()) {
                break; // unbalanced; the runtime label checks trap instead
            }
            const std::size_t opener = open.back();
            info[opener].elseIndex = i;
            break;
        }
        case Opcode::End: {
            if (open.empty()) {
                break; // unbalanced; the runtime label checks trap instead
            }
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

    // Target canvas and scale (spec §8.1). The size cap is a runtime defense:
    // a corrupt meta must not trigger a multi-gigabyte allocation.
    const u32 targetW = inputs.sizeX != 0 ? inputs.sizeX : file.meta.sizeX;
    const u32 targetH = inputs.sizeY != 0 ? inputs.sizeY : file.meta.sizeY;
    if (targetW > kMaxCanvasSide || targetH > kMaxCanvasSide) {
        throw Trap("canvas too large", 0);
    }
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

    // Global output slots, resolved names, and slot owners: root outputs
    // first, then each part's outputs in part order (spec §4.3/§9.9).
    const auto addSlot = [&](const OutputDecl& decl, const ConstPool& pool, u16 owner) {
        detail::OutputSlot slot;
        slot.kind = decl.kind;
        slot.isList = decl.isList;
        impl_->ex.outputSlots.push_back(std::move(slot));
        impl_->ex.outputNames.push_back(pool.strings[decl.nameIndex]);
        impl_->ex.slotOwner.push_back(owner);
    };
    for (const auto& decl : file.meta.outputs) {
        addSlot(decl, file.pool, detail::kNoPartIndex);
    }
    for (std::size_t p = 0; p < file.code.parts.size(); ++p) {
        for (const auto& decl : file.code.parts[p].outputs) {
            addSlot(decl, file.pool, static_cast<u16>(p));
        }
    }
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
        // 1 unit per instruction; composite canvas ops charge their visit
        // counts themselves, up front (spec §8.3).
        ex.spend(1);
        if (!detail::dispatch(ex, ins)) {
            ex.trap("reserved opcode");
        }
    }
}

const Canvas& VM::canvas() const { return impl_->ex.canvas; }
u64 VM::fuel() const { return impl_->ex.fuel; }
u64 VM::fuelBudget() const { return impl_->ex.budget; }
const std::vector<Value>& VM::debugStack() const { return impl_->ex.entryStack; }

GenerationResult VM::result() const {
    GenerationResult result;
    result.image = impl_->ex.canvas;
    const auto& slots = impl_->ex.outputSlots;
    result.outputs.reserve(slots.size());
    for (std::size_t i = 0; i < slots.size(); ++i) {
        NamedOutput output;
        output.name = impl_->ex.outputNames[i];
        output.kind = slots[i].kind;
        output.isList = slots[i].isList;
        output.points = slots[i].points;
        output.rects = slots[i].rects;
        output.circles = slots[i].circles;
        result.outputs.push_back(std::move(output));
    }
    return result;
}

const NamedOutput* GenerationResult::find(std::string_view name) const {
    for (const auto& output : outputs) {
        if (output.name == name) {
            return &output;
        }
    }
    return nullptr;
}

} // namespace iprop
