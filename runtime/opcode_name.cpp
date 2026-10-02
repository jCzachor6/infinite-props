#include "iprop/OpcodeSpec.hpp"

namespace iprop {
const char* opcodeMnemonic(Opcode opcode) {
    const auto* spec = findOpcode(opcode);
    return spec ? spec->mnemonic : nullptr;
}
OperandKind operandKind(Opcode opcode) {
    const auto* spec = findOpcode(opcode);
    return spec ? spec->operand : OperandKind::None;
}
} // namespace iprop
