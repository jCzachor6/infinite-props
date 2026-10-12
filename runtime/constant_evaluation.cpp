#include "iprop/VmInternals.hpp"
#include <limits>

namespace iprop::detail {

bool isConstantOperation(const OpcodeSpec& operation) {
    const auto& effects = operation.effects;
    return operation.operand == OperandKind::None && operation.stack && operation.stack->result &&
        !effects.conservative && !effects.writesCanvas && !effects.writesOutputs &&
        (effects.rng == RngEffect::None || effects.rng == RngEffect::SeededComputation);
}

std::optional<Value> evaluateConstantOperation(Opcode op, std::span<const Value> args) {
    const auto* operation = findOpcode(op);
    if (!operation || !isConstantOperation(*operation)) return std::nullopt;
    const auto& signature = *operation->stack;
    if (args.size() != signature.inputCount) return std::nullopt;
    for (std::size_t i = 0; i < args.size(); ++i)
        if (!accepts(signature.inputs[i], static_cast<ValueType>(args[i].index()))) return std::nullopt;

    // Only the value-operation groups are dispatched: no calls, control flow,
    // locals, metadata, output slots or rendering can run here. One frame
    // supplies stack access and located traps to the existing VM adapters.
    LoadedFile file;
    file.code.functions.emplace_back();
    Execution execution(file);
    execution.frames.emplace_back();
    execution.frame().stack.assign(args.begin(), args.end());
    execution.budget = execution.fuel = std::numeric_limits<u64>::max();
    const Instruction instruction{op, 0, 0};
    if (!(stepArithmetic(execution, instruction) || stepVector(execution, instruction) ||
          stepSdf(execution, instruction) || stepColor(execution, instruction) ||
          stepCurves(execution, instruction) || stepSeeded(execution, instruction))) return std::nullopt;
    if (execution.frame().stack.size() != 1) throw std::logic_error("constant operation result shape");
    return execution.frame().stack.back();
}

} // namespace iprop::detail
