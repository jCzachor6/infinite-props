#pragma once

#include "iprop/Constants.hpp"
#include "iprop/Types.hpp"

#include <cstddef>
#include <span>

namespace iprop {

// One decoded instruction: opcode, aux byte, u16 operand (spec §0 encoding).
struct Instruction {
    Opcode opcode = Opcode::Nop;
    u8 aux = 0;
    u16 operand = 0;
};

// Decodes 4 bytes (little-endian) per the spec §0 instruction encoding.
// The span must hold at least 4 bytes.
inline Instruction decodeInstruction(std::span<const u8> bytes) {
    return {static_cast<Opcode>(bytes[0]), bytes[1],
            static_cast<u16>(bytes[2] | (static_cast<u16>(bytes[3]) << 8))};
}

// The spec §9 mnemonic of an opcode, or nullptr for reserved opcodes.
const char* opcodeMnemonic(Opcode opcode);

// What an instruction's u16 operand means (spec §9). Used by the validator,
// the disassembler, the assembler and the compiler's pool and local remapping.
enum class OperandKind {
    None,              // unused and ignored
    Zero,              // unused and required to be zero
    IntConst,          // const.i32: const-pool index
    FloatConst,        // const.f32: const-pool index
    ColorConst,        // const.color: const-pool index
    Local,             // local.get/set/tee, layer.begin: local index
    TypeDescriptor,    // list/sampler/stepper operations: type descriptor index
    ListConst,         // const.list: collection constant index
    SamplerConst,      // const.sampler: sampler constant index
    StepperConst,      // const.stepper: stepper constant index
    DistributionConst, // const.distribution: distribution constant index
    ResourceExport,    // out.mask/layer, part.get.mask/layer: resource export index
    Immediate,         // distribution.begin/append, surface.paint*: an encoded value
    Name,              // call/part.draw/out.*/part.get.*: resolved at assembly
    Depth,             // br/br_if: branch depth
    Count,             // poly: vertex count
};

OperandKind operandKind(Opcode opcode);

} // namespace iprop
