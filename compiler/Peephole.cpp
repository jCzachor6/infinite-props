#include "Optimizer.hpp"
#include "ConstantEvaluation.hpp"
#include "iprop/OpcodeSpec.hpp"

#include <bit>

namespace iprop::compiler {
namespace {
struct Operand {
    std::optional<Value> value;
    std::size_t start = 0, end = 0;
};

void foldFunction(Plan& plan, PlannedFunction& fn) {
    std::vector<Instruction> code;
    std::vector<u32> lines;
    std::vector<Operand> stack;
    std::vector<std::optional<Value>> locals(fn.params.size() + fn.locals.size());
    const auto append = [&](Instruction ins, u32 line) { code.push_back(ins); lines.push_back(line); };
    const auto constant = [&](auto&& self, const Value& value, u32 line) -> void {
        if (const auto* integer = std::get_if<i32>(&value)) {
            append({Opcode::ConstI32, 0, static_cast<u16>(plan.pools.addInt(static_cast<u32>(*integer)))}, line);
        } else if (const auto* floating = std::get_if<f32>(&value)) {
            append({Opcode::ConstF32, 0, static_cast<u16>(plan.pools.addFloat(std::bit_cast<u32>(*floating)))}, line);
        } else if (const auto* color = std::get_if<Color>(&value)) {
            append({Opcode::ConstColor, 0, static_cast<u16>(plan.pools.addColor(*color))}, line);
        } else {
            const auto vector = std::get<Vec2>(value);
            self(self, Value(vector.x), line); self(self, Value(vector.y), line);
            append({Opcode::Vec2Make, 0, 0}, line);
        }
    };
    for (std::size_t index = 0; index < fn.code.size(); ++index) {
        const auto ins = fn.code[index]; const auto op = ins.opcode; const auto line = fn.sourceLines[index];
        const auto start = code.size();
        std::optional<Value> value;
        if (op == Opcode::ConstI32) value = static_cast<i32>(plan.pools.intsValue().at(ins.operand));
        if (op == Opcode::ConstF32) value = std::bit_cast<f32>(plan.pools.floatsValue().at(ins.operand));
        if (op == Opcode::ConstColor) value = plan.pools.colorsValue().at(ins.operand);
        if (op == Opcode::LocalGet) value = locals.at(ins.operand);
        if (value) {
            constant(constant, *value, line);
            stack.push_back({value, start, code.size()}); continue;
        }
        if (op == Opcode::LocalGet) { append(ins, line); stack.push_back({{}, start, code.size()}); continue; }
        if (op == Opcode::LocalSet || op == Opcode::LocalTee) {
            locals.at(ins.operand) = stack.empty() ? std::nullopt : stack.back().value;
            if (op == Opcode::LocalSet && !stack.empty()) stack.pop_back();
            append(ins, line); continue;
        }
        if (op == Opcode::Drop) { if (!stack.empty()) stack.pop_back(); append(ins, line); continue; }
        if (op == Opcode::Dup) {
            if (!stack.empty()) stack.push_back({stack.back().value, start, start + 1});
            append(ins, line); continue;
        }
        const auto* spec = findOpcode(op);
        if (!spec->stack) {
            stack.clear();
            // Any control transfer ends a straight-line constant region.
            // Calls preserve caller locals, but have metadata-defined stacks.
            if (op != Opcode::Call && op != Opcode::PartDraw)
                std::fill(locals.begin(), locals.end(), std::nullopt);
            append(ins, line); continue;
        }
        const auto& signature = *spec->stack;
        if (stack.size() < signature.inputCount) { stack.clear(); append(ins, line); continue; }
        const auto first = stack.size() - signature.inputCount;
        std::vector<Value> args;
        std::size_t from = start;
        bool contiguous = true;
        for (std::size_t n = first; n < stack.size(); ++n) {
            if (!stack[n].value) { contiguous = false; break; }
            if (n == first) from = stack[n].start;
            if (n > first && stack[n - 1].end != stack[n].start) contiguous = false;
            args.push_back(*stack[n].value);
        }
        if (signature.inputCount && !stack.empty() && stack.back().end != start) contiguous = false;
        auto result = contiguous && signature.result ? evaluateConstant(op, args) : std::nullopt;
        stack.resize(first);
        if (result) {
            code.resize(from); lines.resize(from); constant(constant, *result, line);
            stack.push_back({result, from, code.size()});
        } else {
            append(ins, line);
            if (signature.result) stack.push_back({{}, start, code.size()});
        }
    }
    // local.set n; local.get n leaves the same value while retaining n.
    // A subsequent liveness pass removes the tee if n is no longer read.
    fn.code.clear(); fn.sourceLines.clear();
    for (std::size_t i = 0; i < code.size(); ++i) {
        auto ins = code[i];
        const auto line = lines[i];
        if (ins.opcode == Opcode::LocalSet && i + 1 < code.size() &&
            code[i + 1].opcode == Opcode::LocalGet && code[i + 1].operand == ins.operand) {
            ins.opcode = Opcode::LocalTee;
            ++i;
        }
        fn.code.push_back(ins); fn.sourceLines.push_back(line);
    }
}
} // namespace

void foldAndPeephole(Plan& plan) {
    for (auto& fn : plan.functions) foldFunction(plan, fn);
}
} // namespace iprop::compiler
