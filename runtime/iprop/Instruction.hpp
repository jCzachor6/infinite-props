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

} // namespace iprop
