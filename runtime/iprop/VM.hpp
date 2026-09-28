#pragma once

#include "iprop/Canvas.hpp"
#include "iprop/Instruction.hpp"
#include "iprop/Validator.hpp"
#include "iprop/Value.hpp"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace iprop {

// Host-supplied uniform override (spec §5.4).
struct UniformValue {
    InputType type = InputType::Color;
    Color color{};
    f32 f = 0.f;
    u32 u = 0;
};

// Host invocation inputs (spec §8.1). sizeX/sizeY = 0 means "use meta size".
struct GenerationInputs {
    u32 seed = 0;
    u32 variant = 0;
    u32 archetype = 0;
    u32 sizeX = 0;
    u32 sizeY = 0;
    std::map<std::string, UniformValue> uniforms;
};

// The VM executes a LoadedFile. The caller MUST have run validate() first
// (spec §14: validation completes before execution); the VM does not
// re-validate. Execution consumes the fuel budget (§8.3) and raises Trap (a
// FormatError) with located messages on runtime violations.
class VM {
public:
    VM(const LoadedFile& file, const GenerationInputs& inputs);

    // Executes the entry function. Throws Trap on runtime errors.
    void run();

    const Canvas& canvas() const { return canvas_; }
    u64 fuel() const { return fuel_; }
    u64 fuelBudget() const { return budget_; }

private:
    // Runtime label (spec §9.1): kind 0 = block, 1 = loop, 2 = if.
    struct Label {
        u8 kind = 0;
        std::size_t cont = 0; // loop: start index; block/if: the `end` index
        std::size_t end = 0;  // index of the matching `end`
    };

    // Precomputed matching indices for structured control flow.
    struct ControlInfo {
        std::size_t elseIndex = kNone; // index of `else`, or kNone
        std::size_t endIndex = kNone;  // index of the matching `end`
    };
    static constexpr std::size_t kNone = static_cast<std::size_t>(-1);

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

    void pushRootFrame();
    void execute();
    void step(Instruction ins);
    void callFunction(u16 functionIndex);
    void leaveFrame();
    void ensureControl(u16 functionIndex);
    Value pop();
    void push(Value value);
    void spend(u64 cost);
    [[noreturn]] void trap(std::string message) const;

    const LoadedFile& file_;
    GenerationInputs inputs_;
    Canvas canvas_;
    u64 budget_ = 0;
    u64 fuel_ = 0;
    std::vector<Frame> frames_;
    std::vector<std::vector<ControlInfo>> control_; // per function index
};

} // namespace iprop
