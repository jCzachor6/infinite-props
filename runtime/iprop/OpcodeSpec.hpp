#pragma once

#include "iprop/Code.hpp"
#include "iprop/Instruction.hpp"

#include <array>
#include <initializer_list>
#include <optional>
#include <string_view>

namespace iprop {

enum class StackType : u8 { I32 = 1, F32 = 2, Color = 4, Vec2 = 8, ScalarOrVec2 = 10, Distribution = 16 };
constexpr bool accepts(StackType expected, ValueType actual) {
    return actual.isScalar() && (static_cast<u8>(expected) & (1u << static_cast<u8>(actual))) != 0;
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

enum class AuxPolicy { Zero, PartTransform, SurfaceMode };

// Intrinsic effects on validated source, excluding argument-expression effects
// and execution-wide resource failures. Seeded computations do not use the
// current stream; isolated calls restore it after their own draw/callee.
enum class RngEffect { None, ReadCurrent, AdvanceCurrent, PushSeededStream,
    RestoreStream, SeededComputation, IsolatedSeededStream, SharedCallee, IsolatedCallee };
enum class EffectCheck : u16 { NumericRange = 1, StreamStackUnderflow = 2,
    PartRenderArguments = 4, Callee = 8, PartOutputMissing = 16,
    DivideByZero = 32, IntegerOverflow = 64, PartOutputContext = 128, IndexRange = 256, UnassignedHandle = 512, ScopeDepth = 1024 };
struct OperationEffects {
    RngEffect rng = RngEffect::None;
    bool writesCanvas = false;
    bool writesOutputs = false;
    u16 checks = 0;
    bool conservative = false;
    bool operator==(const OperationEffects&) const = default;
};
const char* rngEffectName(RngEffect effect);
const char* effectCheckName(EffectCheck check);

struct OpcodeSpec {
    Opcode opcode;
    const char* mnemonic;
    OperandKind operand = OperandKind::None;
    AuxPolicy aux = AuxPolicy::Zero;
    // Absent for control flow, metadata-dependent or polymorphic stack ops.
    std::optional<StackSignature> stack;
    OperationEffects effects;
};

std::span<const OpcodeSpec> opcodeSpecs();
const OpcodeSpec* findOpcode(Opcode opcode);
const OpcodeSpec* findOpcode(std::string_view mnemonic);

} // namespace iprop
