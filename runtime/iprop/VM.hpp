#pragma once

#include "iprop/Canvas.hpp"
#include "iprop/Outputs.hpp"
#include "iprop/Validator.hpp"
#include "iprop/Value.hpp"

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace iprop {

// Host-supplied uniform override (spec §5.4).
struct UniformValue {
    InputType type = InputType::Color;
    Color color{};
    f32 f = 0.f;
    u32 u = 0;
};

// One named output slot as delivered to the host (spec §5.4: "the name is
// the contract"). Singular slots hold at most one value; list slots hold
// every appended value in program order.
struct NamedOutput {
    std::string name;
    OutputKind kind = OutputKind::Point;
    bool isList = false;
    std::vector<OutputPoint> points;
    std::vector<OutputRect> rects;
    std::vector<OutputCircle> circles;
};

// The generation result (spec §8.1/§5.4): the image plus every declared
// structured output, in global slot order (root outputs first, then parts,
// spec §4.3).
struct GenerationResult {
    Canvas image;
    std::vector<NamedOutput> outputs;

    const NamedOutput* find(std::string_view name) const;
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
    ~VM();

    VM(VM&&) noexcept;
    VM& operator=(VM&&) noexcept;

    VM(const VM&) = delete;
    VM& operator=(const VM&) = delete;

    // Executes the entry function. Throws Trap on runtime errors.
    void run();

    const Canvas& canvas() const;
    u64 fuel() const;       // remaining budget
    u64 fuelBudget() const; // total budget from meta.fuel

    // Tooling hook: the entry frame's operand stack as it was when run()
    // finished (empty for a validated draw, which has no results). Useful
    // for debuggers and the IDE inspector; not part of the generation
    // contract.
    const std::vector<Value>& debugStack() const;

    // The generation result (spec §5.4/§8.1): the canvas plus every
    // declared structured output, named, in global slot order. Call after
    // run().
    GenerationResult result() const;

    // Consumes the successful generation once, moving its canvas, names and
    // output payloads. Throws std::logic_error before a successful run or
    // after consumption. If allocating the output table fails, the VM remains
    // intact and this call can be retried. After success, run(), canvas() and
    // result() reject further access; fuel/debugStack remain available.
    GenerationResult takeResult();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace iprop
