#pragma once

#include "iprop/Canvas.hpp"
#include "iprop/Validator.hpp"

#include <cstdint>
#include <map>
#include <memory>
#include <string>

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

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace iprop
