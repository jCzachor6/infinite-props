#pragma once

// Internal execution machinery shared by the VM and the per-group instruction
// implementations (stack.cpp, control.cpp, arithmetic.cpp, ...). Not part of
// the public API; consumers use VM from iprop/VM.hpp.

#include "iprop/Canvas.hpp"
#include "iprop/Instruction.hpp"
#include "iprop/Outputs.hpp"
#include "iprop/PartRenderPlan.hpp"
#include "iprop/Validator.hpp"
#include "iprop/Value.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace iprop::detail {

// Sentinel: no part called yet / a slot owned by the root asset.
inline constexpr u16 kNoPartIndex = 0xFFFFu;

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

// One structured output slot (global numbering, spec §4.3). Singular slots
// hold at most one value (a later write replaces it); list slots append in
// program order.
struct OutputSlot {
    OutputKind kind = OutputKind::Point;
    bool isList = false;
    std::vector<OutputPoint> points;
    std::vector<OutputRect> rects;
    std::vector<OutputCircle> circles;
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
    // `call` frames share the caller's stream like a reference (spec §8.2):
    // on return, their final stream state propagates back to the caller.
    // Root and part frames start their own stream and do not propagate.
    bool shareStream = false;
    // Part frames render into their own sub-canvas (spec §10).
    bool isPart = false;
    // The index of the most recently called part in this frame (spec §9.9);
    // kNoPartIndex before any part call.
    u16 lastPart = kNoPartIndex;
};

// Saved caller state for one part call (spec §10): the caller's canvas is
// moved aside while the part renders into its own sub-canvas, then restored
// and blitted.
struct PartCallState {
    u16 partIndex = kNoPartIndex;
    Canvas savedCanvas;
    PartRenderPlan render;
};

// All mutable execution state of one generation (spec §8.4). The public VM
// owns one Execution; group step functions operate on it.
struct Execution {
    const LoadedFile& file;
    Canvas canvas;
    u64 budget = 0;
    u64 fuel = 0;
    std::vector<Frame> frames;
    // Snapshot before dispatch: call/part operations may advance the frame PC
    // before a trap. Structured locations identify the failing instruction.
    ErrorLocation activeInstruction;
    std::vector<std::vector<ControlInfo>> control; // per function index
    // Global output slots, their resolved names, and the owning part index
    // (kNoPartIndex for root slots), spec §4.3/§9.9.
    std::vector<OutputSlot> outputSlots;
    std::vector<std::string> outputNames;
    std::vector<u16> slotOwner;
    // Saved caller state of active part calls, mirroring part frames.
    std::vector<PartCallState> partCalls;
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
    Vec2 popVec2();

    // Fuel accounting (spec §8.3): traps with "fuel exhausted".
    void spend(u64 cost);

    // Frame lifecycle.
    void callFunction(u16 functionIndex);
    void callPart(u16 partIndex, u8 transform);
    void leaveFrame();

    // Precomputes the matching else/end indices of a function's control flow.
    void ensureControl(u16 functionIndex);

    [[noreturn]] void trap(std::string message, ErrorReason reason) const;
};

// Composite one pixel onto the current canvas with the §11.1 alpha rule
// (shared by the canvas ops and the part blit).
void compositePixel(Execution& ex, i32 x, i32 y, const Color& src);

// Per-domain VM adapters own stack access, traps, results and ip advancement.
// Mathematical kernels are independent of Execution (iprop/math/*.hpp).
bool dispatch(Execution& ex, Instruction ins);
bool stepCore(Execution& ex, Instruction ins);
bool stepStack(Execution& ex, Instruction ins);
bool stepControl(Execution& ex, Instruction ins);
bool stepArithmetic(Execution& ex, Instruction ins);
bool stepVector(Execution& ex, Instruction ins);
bool stepSdf(Execution& ex, Instruction ins);
bool stepColor(Execution& ex, Instruction ins);
bool stepCurves(Execution& ex, Instruction ins);
bool stepSeeded(Execution& ex, Instruction ins);
bool stepCanvas(Execution& ex, Instruction ins);
bool stepOutputs(Execution& ex, Instruction ins);
bool stepParts(Execution& ex, Instruction ins);

} // namespace iprop::detail
