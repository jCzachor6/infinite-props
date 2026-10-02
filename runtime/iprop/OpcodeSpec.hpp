#pragma once

#include "iprop/Code.hpp"
#include "iprop/Instruction.hpp"

#include <array>
#include <initializer_list>
#include <optional>
#include <string_view>

namespace iprop {

enum class StackType : u8 { I32 = 1, F32 = 2, Color = 4, Vec2 = 8, ScalarOrVec2 = 10 };
constexpr bool accepts(StackType expected, ValueType actual) {
    return (static_cast<u8>(expected) & (1u << static_cast<u8>(actual))) != 0;
}

struct StackSignature {
    // Input order is deepest first, like the VM function signatures.
    std::array<StackType,6> inputs{};
    std::size_t inputCount = 0;
    std::optional<ValueType> result;
    constexpr StackSignature(std::initializer_list<StackType> params,
                             std::optional<ValueType> output = {}) : result(output) {
        for (const auto p : params) inputs.at(inputCount++) = p;
    }
};

enum class AuxPolicy { Zero, PartTransform };
struct OpcodeSpec {
    Opcode opcode;
    const char* mnemonic;
    u16 minimumMinor;
    OperandKind operand = OperandKind::None;
    AuxPolicy aux = AuxPolicy::Zero;
    // Absent for control flow, metadata-dependent or polymorphic stack ops.
    std::optional<StackSignature> stack;
};

std::span<const OpcodeSpec> opcodeSpecs();
const OpcodeSpec* findOpcode(Opcode opcode);
const OpcodeSpec* findOpcode(std::string_view mnemonic);

} // namespace iprop
