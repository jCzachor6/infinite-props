#pragma once

// Internal execution machinery shared by the VM and the per-group instruction
// implementations (stack.cpp, control.cpp, arithmetic.cpp, ...). Not part of
// the public API; consumers use VM from iprop/VM.hpp.

#include "iprop/Canvas.hpp"
#include "iprop/Instruction.hpp"
#include "iprop/Validator.hpp"
#include "iprop/Value.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace iprop::detail {

// Runtime label (spec §9.1): kind 0 = block, 1 = loop, 2 = if. stackHeight is
// the operand stack height at entry; branches restore it (WASM semantics).
struct Label {
    u8 kind = 0;
    std::size_t cont = 0; // loop: body start; block/if: unused
    std::size_t end = 0;  // index of the matching `end`
    std::size_t stackHeight = 0;
};

// Precomputed matching indices for structured control flow.
struct ControlInfo {
    static constexpr std::size_t kNone = static_cast<std::size_t>(-1);
    std::size_t elseIndex = kNone; // index of `else`, or kNone
    std::size_t endIndex = kNone;  // index of the matching `end`
};

// One call frame (spec §8.2).
struct Frame {
    u16 functionIndex = 0;
    std::vector<Value> locals; // params first, then declared locals
    std::vector<Value> stack;  // operand stack
    u32 stream = 0;            // current stream state
    std::vector<u32> savedStreams;
    std::size_t ip = 0; // instruction index within the function stream
    std::vector<Label> labels;
};

// All mutable execution state of one generation (spec §8.4). The public VM
// owns one Execution; group step functions operate on it.
struct Execution {
    const LoadedFile& file;
    Canvas canvas;
    u64 budget = 0;
    u64 fuel = 0;
    std::vector<Frame> frames;
    std::vector<std::vector<ControlInfo>> control; // per function index
    // The entry frame's stack when it exits; a tooling hook for debuggers
    // and tests (valid draw programs leave it empty).
    std::vector<Value> entryStack;

    explicit Execution(const LoadedFile& f) : file(f) {}

    const Function& fn() const { return file.code.functions[frames.back().functionIndex]; }
    Frame& frame() { return frames.back(); }

    // Operand stack access with runtime traps (defense in depth: validated
    // files never hit these, unvalidated input must still trap, never UB).
    Value pop();
    Value peek();
    void push(Value value);

    // Typed pops; trap on an empty stack or a type mismatch.
    i32 popI32();
    f32 popF32();
    Color popColor();

    // Fuel accounting (spec §8.3): traps with "fuel exhausted".
    void spend(u64 cost);

    // Frame lifecycle.
    void callFunction(u16 functionIndex);
    void leaveFrame();

    // Precomputes the matching else/end indices of a function's control flow.
    void ensureControl(u16 functionIndex);

    [[noreturn]] void trap(std::string message) const;
};

// Per-group instruction dispatchers. Each returns true when it handled the
// opcode; dispatch() falls through to a trap otherwise. Groups arrive with
// their phases: core frame ops (vm.cpp, Phase 7), stack.cpp (Phase 7),
// control.cpp (Phase 8), arithmetic.cpp (Phase 8 minimal subset + Phase 9
// remainder), seeded.cpp (Phase 10), canvas.cpp (Phase 11), outputs.cpp
// (Phase 12), parts.cpp (Phase 13).
bool dispatch(Execution& ex, Instruction ins);
bool stepCore(Execution& ex, Instruction ins);
bool stepStack(Execution& ex, Instruction ins);
bool stepControl(Execution& ex, Instruction ins);
bool stepArithmetic(Execution& ex, Instruction ins);
bool stepSeeded(Execution& ex, Instruction ins);

} // namespace iprop::detail
