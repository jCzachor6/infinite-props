#pragma once

#include "iprop/Instruction.hpp"
#include "iprop/Value.hpp"
#include <optional>
#include <span>

namespace iprop::compiler {
std::optional<Value> evaluateConstant(Opcode op, std::span<const Value> args);
} // namespace iprop::compiler
