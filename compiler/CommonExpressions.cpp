#include "Optimizer.hpp"
#include "iprop/OpcodeSpec.hpp"

namespace iprop::compiler {
namespace {
struct Expression {
    std::string key;
    std::size_t start = 0, end = 0;
};
struct Available { u16 slot; u32 version; };

void reuse(PlannedFunction& fn) {
    std::vector<Instruction> code;
    std::vector<u32> lines;
    std::vector<Expression> stack;
    const auto count = fn.params.size() + fn.locals.size();
    std::vector<std::string> bindings(count);
    std::vector<u32> versions(count);
    std::map<std::string, Available> available;
    const auto append = [&](Instruction ins, u32 line) { code.push_back(ins); lines.push_back(line); };
    const auto reset = [&]() {
        stack.clear(); available.clear();
        std::fill(bindings.begin(), bindings.end(), std::string{});
    };
    for (std::size_t n = 0; n < fn.code.size(); ++n) {
        const auto ins = fn.code[n]; const auto op = ins.opcode; const auto line = fn.sourceLines[n];
        const auto start = code.size();
        if (op == Opcode::ConstI32 || op == Opcode::ConstF32 || op == Opcode::ConstColor) {
            append(ins, line);
            stack.push_back({"C" + std::to_string(static_cast<u8>(op)) + ":" + std::to_string(ins.operand), start, code.size()});
            continue;
        }
        if (op == Opcode::LocalGet) {
            const auto key = bindings.at(ins.operand).empty()
                ? "L" + std::to_string(ins.operand) + ":" + std::to_string(versions.at(ins.operand))
                : bindings.at(ins.operand);
            append(ins, line); stack.push_back({key, start, code.size()}); continue;
        }
        if (op == Opcode::LocalSet || op == Opcode::LocalTee) {
            const auto key = stack.empty() ? std::string{} : stack.back().key;
            auto& version = versions.at(ins.operand); ++version;
            bindings.at(ins.operand) = key;
            if (!key.empty()) {
                // A stored value is already available without adding scratch
                // locals or speculative evaluation. Bound key/cache memory.
                if (available.size() >= 4096) available.clear();
                available.insert_or_assign(key, Available{ins.operand, version});
            }
            if (op == Opcode::LocalSet && !stack.empty()) stack.pop_back();
            append(ins, line); continue;
        }
        if (op == Opcode::Drop) { if (!stack.empty()) stack.pop_back(); append(ins,line); continue; }
        if (op == Opcode::Dup) {
            if (!stack.empty()) stack.push_back({stack.back().key, start, start + 1});
            append(ins,line); continue;
        }
        const auto* spec = findOpcode(op);
        if (!spec->stack || stack.size() < spec->stack->inputCount) { reset(); append(ins,line); continue; }
        const auto& signature = *spec->stack;
        const auto first = stack.size() - signature.inputCount;
        bool contiguous = discardableInputs(op).has_value() && signature.result.has_value();
        std::string key = "E" + std::to_string(static_cast<u8>(op)) + "(";
        std::size_t from = start;
        for (std::size_t i = first; i < stack.size(); ++i) {
            if (stack[i].key.empty()) contiguous = false;
            if (i == first) from = stack[i].start;
            else if (stack[i - 1].end != stack[i].start) contiguous = false;
            key += stack[i].key + ";";
        }
        key += ")";
        if (signature.inputCount && stack.back().end != start) contiguous = false;
        if (key.size() > 512) contiguous = false;
        stack.resize(first);
        if (!contiguous) key.clear();
        const auto prior = available.find(key);
        if (contiguous && prior != available.end() && versions.at(prior->second.slot) == prior->second.version) {
            // The value still resides in that local. Replacing only a
            // contiguous pure tree cannot erase intervening observable ops.
            code.resize(from); lines.resize(from);
            append({Opcode::LocalGet, 0, prior->second.slot}, line);
        } else append(ins,line);
        if (signature.result) stack.push_back({key, contiguous ? from : start, code.size()});
    }
    fn.code = std::move(code); fn.sourceLines = std::move(lines);
}
} // namespace

void reuseCommonExpressions(Plan& plan) {
    for (auto& fn : plan.functions) reuse(fn);
}
} // namespace iprop::compiler
