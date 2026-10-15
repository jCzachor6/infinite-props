#include "ConstantEvaluation.hpp"
#include "iprop/VmInternals.hpp"

#include <cmath>

namespace iprop::compiler {
std::optional<Value> evaluateConstant(Opcode op, std::span<const Value> args) {
    // The VM's own handlers evaluate the operation, so folding cannot drift
    // from execution. A trap stays in the program: it fires only if reached.
    std::optional<Value> result;
    try {
        result = detail::evaluateConstantOperation(op, args);
    } catch (const Trap&) {
        return std::nullopt;
    }
    if (!result) return std::nullopt;
    // Historical scalar NaN payloads are transported unchanged: a component
    // read copies existing bits. Leave non-finite arithmetic to the VM
    // instead of folding host NaNs.
    const bool transport = op == Opcode::Vec2X || op == Opcode::Vec2Y;
    if (const auto* f = std::get_if<f32>(&*result)) return transport || std::isfinite(*f) ? result : std::nullopt;
    // Only values with a constant-pool encoding can replace instructions.
    if (std::holds_alternative<i32>(*result) || std::holds_alternative<Color>(*result) ||
        std::holds_alternative<Vec2>(*result)) return result;
    return std::nullopt;
}
} // namespace iprop::compiler
