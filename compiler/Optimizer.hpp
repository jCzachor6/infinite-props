#pragma once

#include "Plan.hpp"

namespace iprop::compiler {

// These passes operate on emitted typed IR. They never change the VM or AST.
void eliminateDeadCode(Plan& plan);
void compactPools(Plan& plan);
void foldAndPeephole(Plan& plan);
void reuseCommonExpressions(Plan& plan);

// A one-result, non-trapping instruction whose unused value may be removed.
std::optional<std::size_t> discardableInputs(Opcode opcode);

} // namespace iprop::compiler
