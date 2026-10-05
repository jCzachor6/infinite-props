#include "Optimizer.hpp"

namespace iprop::compiler {
namespace {
bool integer(const Plan& plan, Instruction ins, u32 bits) {
    return ins.opcode == Opcode::ConstI32 && plan.pools.intsValue().at(ins.operand) == bits;
}
bool nonnegative(const Plan& plan, const std::vector<Instruction>& code, std::size_t producer) {
    const auto ins = code[producer];
    if (ins.opcode == Opcode::ConstI32) return plan.pools.intsValue().at(ins.operand) <= 0x7fffffffu;
    if (producer == 0 || code[producer - 1].opcode != Opcode::ConstI32) return false;
    const auto bits = plan.pools.intsValue().at(code[producer - 1].operand);
    if (ins.opcode == Opcode::I32And) return bits <= 0x7fffffffu;
    return ins.opcode == Opcode::I32ShrU && (bits & 31u) != 0;
}
} // namespace

void reduceStrength(Plan& plan) {
    for (auto& fn : plan.functions) {
        auto& code = fn.code;
        for (std::size_t i = 1; i < code.size(); ++i) {
            if (!integer(plan, code[i - 1], 2)) continue;
            if (code[i].opcode == Opcode::I32Mul) {
                // Modulo-2^32 addition equals multiplication by 2, including
                // negative inputs and wrapping overflow. Evaluate once.
                code[i - 1] = {Opcode::Dup, 0, 0};
                code[i] = {Opcode::I32Add, 0, 0};
            } else if (code[i].opcode == Opcode::I32DivU ||
                       (code[i].opcode == Opcode::I32DivS && i >= 2 && nonnegative(plan, code, i - 2))) {
                // Signed division truncates toward zero. A plain shift is
                // valid only with an actual nonnegative range proof.
                code[i - 1].operand = static_cast<u16>(plan.pools.addInt(1));
                code[i].opcode = Opcode::I32ShrU;
            }
        }
    }
}
} // namespace iprop::compiler
