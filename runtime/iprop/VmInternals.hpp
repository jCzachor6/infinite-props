#pragma once

// Internal execution machinery shared by the VM and the per-group instruction
// implementations (VmScalarSteps.hpp, seeded.cpp, canvas.cpp, ...). Not part of
// the public API; consumers use VM from iprop/VM.hpp.

#include "iprop/Canvas.hpp"
#include "iprop/Instruction.hpp"
#include "iprop/OpcodeSpec.hpp"
#include "iprop/Outputs.hpp"
#include "iprop/PartOutput.hpp"
#include "iprop/PartRenderPlan.hpp"
#include "iprop/Validator.hpp"
#include "iprop/Value.hpp"
#include "iprop/ExecutionLimits.hpp"
#include "iprop/FuelTariff.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace iprop::detail {

// Sentinel: no part called yet / a slot owned by the root asset.
inline constexpr u16 kNoPartIndex = 0xFFFFu;

// Runtime label: kind 0 = block, 1 = loop, 2 = if, 3 = layer, 4 = clip. stackHeight is
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
    // Published values, in root asset coordinates with a parts section.
    std::vector<OutputPoint> points;
    std::vector<OutputRect> rects;
    std::vector<OutputCircle> circles;
    std::shared_ptr<PartOutputData> parts;
    // The latest value as part.get returns it to the owner's caller.
    OutputPoint readPoint{};
    OutputRect readRect{};
    OutputCircle readCircle{};
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
    RenderTarget savedTarget;
    PartRenderPlan render;
};

struct LayerScope {
    std::size_t frameDepth = 0;
    std::size_t labelDepth = 0;
    u16 local = 0;
    RenderTarget savedTarget;
    bool isClip = false;
    std::size_t savedClipDepth = 0;
};

// All mutable execution state of one generation (spec §8.4). The public VM
// owns one Execution; group step functions operate on it.
struct Execution {
    const LoadedFile& file;
    RenderTarget target;
    RenderTarget rootTarget; // preserves the existing failed-run canvas tooling hook
    u64 budget = 0;
    u64 fuel = 0;
    u64 fuelUnits = 0, fuelPaid = 0;
    u8 fuelRate = 100;
    std::shared_ptr<WorkBudget> workBudget = std::make_shared<WorkBudget>();
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
    // Shared immutable snapshots, indexed only by the resource-export table.
    std::vector<Value> resourceSlots;
    // Saved caller state of active part calls, mirroring part frames.
    std::vector<PartCallState> partCalls;
    std::vector<LayerScope> layerScopes;
    // The entry frame's stack when it exits; a tooling hook for debuggers
    // and tests (valid draw programs leave it empty).
    std::vector<Value> entryStack;
    ResourceArena resources;
    // Whole-generation identity, shared by helper and static component calls.
    // Releasing a view or a resource never refunds an instance creation.
    u32 dynamicPartCreations = 0;
    u32 exportedParts = 0; // all slots/static calls share the append limit

    // Finished call frames, kept for their storage: a helper call reuses the
    // locals, stack and label buffers of an earlier one.
    std::vector<Frame> spareFrames;

    explicit Execution(const LoadedFile& f) : file(f) { resources.workBudget = workBudget; }
    Canvas& canvas() { return target.canvas(); }
    const Canvas& canvas() const { return target.canvas(); }

    const Function& fn() const { return file.code.functions[frames.back().functionIndex]; }
    Frame& frame() { return frames.back(); }

    // Operand stack access with runtime traps (defense in depth: validated
    // files never hit these, unvalidated input must still trap, never UB).
    // The checks and the scalar cases are inline; every trap is out of line.
    IPROP_ALWAYS_INLINE Value pop() {
        auto& stack = frames.back().stack;
        if (stack.empty()) [[unlikely]] stackUnderflow();
        Value value = std::move(stack.back());
        stack.pop_back();
        return value;
    }
    Value peek() {
        auto& stack = frames.back().stack;
        if (stack.empty()) [[unlikely]] stackUnderflow();
        return stack.back();
    }
    // Constructs the pushed value in place: scalars never pass through an
    // intermediate Value.
    template<class T> IPROP_ALWAYS_INLINE void push(T&& value) {
        auto& stack = frames.back().stack;
        if (stack.size() >= kMaxStackDepth) [[unlikely]] stackLimit();
        stack.emplace_back(std::forward<T>(value));
    }

    // Typed pops; trap on an empty stack or a type mismatch. A scalar is read
    // in place and popped, without moving the whole Value.
    template<class T> IPROP_ALWAYS_INLINE T popScalar(const char* mismatch) {
        auto& stack = frames.back().stack;
        if (stack.empty()) [[unlikely]] stackUnderflow();
        const auto* value = std::get_if<T>(&stack.back());
        if (!value) [[unlikely]] stackType(mismatch);
        const T result = *value;
        stack.pop_back();
        return result;
    }
    IPROP_ALWAYS_INLINE i32 popI32() { return popScalar<i32>("stack type mismatch: expected i32"); }
    IPROP_ALWAYS_INLINE f32 popF32() { return popScalar<f32>("stack type mismatch: expected f32"); }
    IPROP_ALWAYS_INLINE Color popColor() { return popScalar<Color>("stack type mismatch: expected color"); }
    IPROP_ALWAYS_INLINE Vec2 popVec2() { return popScalar<Vec2>("stack type mismatch: expected vec2"); }
    Distribution popDistribution() {
        return popScalar<Distribution>("stack type mismatch: expected distribution");
    }
    // Every handle shares one rule: another alternative is a stack type
    // error, an unassigned handle a runtime violation.
    template<class H> H popHandle(const char* name) {
        Value value = pop();
        auto* handle = std::get_if<H>(&value);
        if (!handle) trap(std::string("stack type mismatch: expected ") + name, ErrorReason::StackType);
        bool assigned = false;
        if constexpr (requires { handle->buffer; }) assigned = static_cast<bool>(handle->buffer);
        else assigned = static_cast<bool>(handle->data);
        if (!assigned) trap(std::string(name) + " is unassigned", ErrorReason::RuntimeViolation);
        return std::move(*handle);
    }
    // Surface handles additionally check their storage (and masks their
    // mapping and raster scale).
    LayerValue popLayer();
    MaskValue popMask(bool matchTarget = true);
    void allocateResource(u64 bytes);

    // Fuel accounting (spec §8.3): traps with "fuel exhausted". Most opcodes
    // pay the plain tariff; a discounted one rounds its cumulative units.
    IPROP_ALWAYS_INLINE void spend(u64 cost) {
        if (fuelRate != 100) [[unlikely]] cost = scaledCost(cost);
        spendRaw(cost);
    }
    IPROP_ALWAYS_INLINE void spendRaw(u64 cost) {
        if (cost > fuel) [[unlikely]] fuelExhausted();
        fuel -= cost;
    }
    void beginInstruction(Opcode op) { fuelRate = kFuelPercent[static_cast<u8>(op)]; fuelUnits = fuelPaid = 0; }
    IPROP_ALWAYS_INLINE void work(u64 units = 1) const { workBudget->location = activeInstruction; workBudget->spend(units); }
    // `count` unit steps at once, exactly as `count` calls of work().
    void workEach(u64 count) const { workBudget->location = activeInstruction; workBudget->spendEach(count); }
    void checkpoint() const { workBudget->checkpoint(); }
    // Reserves all the unit steps of a raster row when nothing in the row can
    // trap: no clip is active, so compositing spends nothing, and the steps
    // are affordable. The caller then visits only the pixels it has to. When
    // this returns false it reserves each step where the per-pixel loop does,
    // so a limit still stops at the same pixel.
    bool prepayRow(u64 steps) const {
        if (!target.clips.empty() || !workBudget->affords(steps)) return false;
        workEach(steps);
        return true;
    }

    // Frame lifecycle.
    // An empty frame, with the buffers of a finished one when there is one.
    Frame spareFrame();
    // A callee's frame: its arguments popped from the caller, then every
    // declared local at its typed zero.
    void popArguments(Frame& next, const Function& callee);
    void zeroLocals(Frame& next, const Function& callee);
    void callFunction(u16 functionIndex);
    void callPart(u16 partIndex, u8 transform);
    void leaveFrame();
    void beginLayer(u16 local, u8 mode);
    void beginClip();
    // Unwind layer and clip scopes together, in reverse entry order. Most
    // label exits have no open capture or clip at all.
    void closeLayers(std::size_t labelDepth) {
        if (!layerScopes.empty() && layerScopes.back().frameDepth == frames.size() &&
            layerScopes.back().labelDepth > labelDepth) [[unlikely]] closeOpenLayers(labelDepth);
    }
    void closeOpenLayers(std::size_t labelDepth);

    // Precomputes the matching else/end indices of a function's control flow.
    void ensureControl(u16 functionIndex) {
        if (functionIndex >= control.size() || control[functionIndex].empty()) [[unlikely]] scanControl(functionIndex);
    }
    void scanControl(u16 functionIndex);

    [[noreturn]] void trap(std::string message, ErrorReason reason) const;
    [[noreturn]] void stackUnderflow() const;
    [[noreturn]] void stackLimit() const;
    [[noreturn]] void stackType(const char* mismatch) const;
    [[noreturn]] void fuelExhausted() const;
    // The increase of the rounded cumulative cost of a discounted opcode.
    u64 scaledCost(u64 cost);
};

// Primitives, part blits and paint use the same rounded RGBA8 kernel.
void compositePixel(Execution& ex, i32 x, i32 y, const Color& src,
    CompositionMode mode = CompositionMode::Inherited);
void paintSurface(Execution& ex, const LayerValue& image,
    CompositionMode mode = CompositionMode::Inherited);
u8 clipCoverage(Execution& ex, i32 x, i32 y);
// Checks a layer or mask view before use: representable i32 bounds, known
// storage matching its extent and a finite positive raster scale, which must
// equal the active target's with matchTarget. Returns the view's bounds.
MaskBounds validateSurfaceView(Execution& ex, const SurfaceBuffer& buffer, const SurfaceMapping& mapping,
                               bool matchTarget);
void weatherPixel(Execution& ex, i32 x, i32 y, Color proposed);

// A prepaid row has no work call per step; this keeps its polls at most
// kWorkPollInterval physical steps apart.
struct PrepaidSteps {
    WorkBudget& budget;
    u32 sincePoll = 0;
    void step() {
        if (++sincePoll == kWorkPollInterval) { sincePoll = 0; budget.poll(); }
    }
};

// Per-domain VM adapters own stack access, traps, results and ip advancement.
// Mathematical kernels are independent of Execution (iprop/math/*.hpp).
// stepStack, stepControl and stepArithmetic are inline (VmScalarSteps.hpp).
bool dispatch(Execution& ex, Instruction ins);
bool stepCore(Execution& ex, Instruction ins);
bool stepVector(Execution& ex, Instruction ins);
bool stepSdf(Execution& ex, Instruction ins);
bool stepColor(Execution& ex, Instruction ins);
bool stepCurves(Execution& ex, Instruction ins);
bool stepSeeded(Execution& ex, Instruction ins);
bool stepCanvas(Execution& ex, Instruction ins);
bool stepOutputs(Execution& ex, Instruction ins);
bool stepParts(Execution& ex, Instruction ins);
bool stepCollections(Execution& ex, Instruction ins);
bool stepSamplers(Execution& ex, Instruction ins);
bool stepSteppers(Execution& ex, Instruction ins);
bool stepPointDistributions(Execution& ex, Instruction ins);
bool stepSurfaces(Execution& ex, Instruction ins);
bool stepMasks(Execution& ex, Instruction ins);
bool stepVoronoi(Execution& ex, Instruction ins);
bool stepContours(Execution& ex, Instruction ins);
bool stepRegions(Execution& ex, Instruction ins);
bool stepDynamicParts(Execution& ex, Instruction ins);
bool stepBlur(Execution& ex, Instruction ins);
bool stepResourceExports(Execution& ex, Instruction ins);
Value drawSampler(Execution& ex, const SamplerData& sampler);

// Samplers and steppers share their instruction mechanics (descriptions.cpp).
// The checked description descriptor named by an instruction operand.
TypeDescriptor descriptionType(Execution& ex, Instruction ins, TypeDescriptor::Kind kind);
// const.sampler/const.stepper: charges a pooled description and its points
// once per generation; the caller pushes the shared handle.
void retainDescriptionConstant(Execution& ex, const CapturedRangeData& description);
// *.value/*.range/*.with: pops typed endpoints and an optional distribution,
// validates them once, charges one description and pushes its handle.
void captureDescription(Execution& ex, TypeDescriptor type, SamplerMode mode);
// Pops a description handle of exactly `type`.
std::shared_ptr<const CapturedRangeData> popDescription(Execution& ex, TypeDescriptor type);
// *.pick.list: checks the count, then the handle, and charges element fuel and
// the list before generating element `index` of `count`.
using DescriptionElement = Value (*)(Execution&, const CapturedRangeData&, i32 index, i32 count);
void pickDescriptionList(Execution& ex, TypeDescriptor type, DescriptionElement element);
// A truncated distribution draw for the unit coefficient u (spec §19.4), shared
// by random draws and stepper quantiles: point/segment work is reserved first,
// failures trap with the sampler rules, then kernel iterations are charged.
double checkedTruncatedDraw(Execution& ex, const Distribution& distribution, double lo, double hi, double u);

// Compile-time evaluation of one stateless value operation. Uses the same
// adapters, kernels and traps as generation, without a canvas or RNG stream.
// Returns nullopt for operations that observe/write generation state.
bool isConstantOperation(const OpcodeSpec& operation);
std::optional<Value> evaluateConstantOperation(Opcode op, std::span<const Value> args);

// Moves or copies a value into a slot; plain scalars assign their alternative
// directly instead of going through the variant's visitor.
IPROP_ALWAYS_INLINE void moveValue(Value& target, Value& source) {
    if (const auto* i = std::get_if<i32>(&source)) target = *i;
    else if (const auto* f = std::get_if<f32>(&source)) target = *f;
    else if (const auto* c = std::get_if<Color>(&source)) target = *c;
    else if (const auto* v = std::get_if<Vec2>(&source)) target = *v;
    else target = std::move(source);
}
IPROP_ALWAYS_INLINE void copyValue(Value& target, const Value& source) {
    if (const auto* i = std::get_if<i32>(&source)) target = *i;
    else if (const auto* f = std::get_if<f32>(&source)) target = *f;
    else if (const auto* c = std::get_if<Color>(&source)) target = *c;
    else if (const auto* v = std::get_if<Vec2>(&source)) target = *v;
    else target = source;
}
inline void appendValue(std::vector<Value>& values, Value& source) {
    if (const auto* i = std::get_if<i32>(&source)) values.emplace_back(*i);
    else if (const auto* f = std::get_if<f32>(&source)) values.emplace_back(*f);
    else if (const auto* c = std::get_if<Color>(&source)) values.emplace_back(*c);
    else if (const auto* v = std::get_if<Vec2>(&source)) values.emplace_back(*v);
    else values.push_back(std::move(source));
}

} // namespace iprop::detail

#include "iprop/VmScalarSteps.hpp"
