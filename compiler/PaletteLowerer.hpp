#pragma once
#include <optional>
#include "ips/Palette.hpp"

namespace iprop::compiler {
class Emitter;
struct PlannedArgument;

// Palette selection owns its temporary lifetimes and control-flow strategy.
// Elements have already been validated and decoded in the compilation plan.
class PaletteLowerer {
public:
    PaletteLowerer(Emitter& emitter, const ips::ResolvedPalette& palette)
        : emitter_(emitter), palette_(palette) {}
    ips::ValueType index(const ips::Expr& index);
    ips::ValueType ramp(const PlannedArgument& t);
private:
    Emitter& emitter_;
    const ips::ResolvedPalette& palette_;
    void select(u16 index, u16 result, std::optional<u16> fraction = {});
};
} // namespace iprop::compiler
