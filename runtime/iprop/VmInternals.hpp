#pragma once

// Internal execution machinery shared by the VM and the per-group instruction
// implementations (stack.cpp, control.cpp, arithmetic.cpp, ...). Not part of
// the public API; consumers use VM from iprop/VM.hpp.

#include "iprop/Canvas.hpp"
#include "iprop/Instruction.hpp"
#include "iprop/OpcodeSpec.hpp"
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

    explicit Execution(const LoadedFile& f) : file(f) {}
    Canvas& canvas() { return target.canvas(); }
    const Canvas& canvas() const { return target.canvas(); }

    const Function& fn() const { return file.code.functions[frames.back().functionIndex]; }
    Frame& frame() { return frames.back(); }

    // Operand stack access with runtime traps (defense in depth: validated
    // files never hit these, unvalidated input must still trap, never UB).
    Value pop();
    Value peek();
    // Constructs the pushed value in place: scalars never pass through an
    // intermediate Value.
    template<class T> void push(T&& value) {
        auto& stack = frames.back().stack;
        if (stack.size() >= kMaxStackDepth) trap("operand stack depth exceeds limit", ErrorReason::StackLimit);
        stack.emplace_back(std::forward<T>(value));
    }

    // Typed pops; trap on an empty stack or a type mismatch.
    i32 popI32();
    f32 popF32();
    Color popColor();
    Vec2 popVec2();
    Distribution popDistribution();
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

    // Fuel accounting (spec §8.3): traps with "fuel exhausted".
    void spend(u64 cost);

    // Frame lifecycle.
    void callFunction(u16 functionIndex);
    void callPart(u16 partIndex, u8 transform);
    void leaveFrame();
    void beginLayer(u16 local, u8 mode);
    void beginClip();
    // Unwind layer and clip scopes together, in reverse entry order.
    void closeLayers(std::size_t labelDepth);

    // Precomputes the matching else/end indices of a function's control flow.
    void ensureControl(u16 functionIndex);

    [[noreturn]] void trap(std::string message, ErrorReason reason) const;
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
bool stepCollections(Execution& ex, Instruction ins);
bool stepSamplers(Execution& ex, Instruction ins);
bool stepSteppers(Execution& ex, Instruction ins);
bool stepPointDistributions(Execution& ex, Instruction ins);
bool stepSurfaces(Execution& ex, Instruction ins);
bool stepMasks(Execution& ex, Instruction ins);
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

} // namespace iprop::detail
