#include "Optimizer.hpp"
#include "iprop/OpcodeSpec.hpp"
#include <algorithm>

namespace iprop::compiler {
namespace {
struct Expression {
    std::string key;
    std::size_t start = 0, end = 0;
};
struct Available { u16 slot; u32 version; std::size_t definition; };
using Counts = std::map<std::string, unsigned>;

void reuse(PlannedFunction& fn, Counts& counts, bool materialize) {
    std::vector<Instruction> code;
    std::vector<u32> lines;
    std::vector<Expression> stack;
    const auto count = fn.params.size() + fn.locals.size();
    std::vector<std::string> bindings(count);
    std::vector<u32> versions(count);
    std::map<std::string, Available> available;
    std::size_t region = 0;
    const auto append = [&](Instruction ins, u32 line) { code.push_back(ins); lines.push_back(line); };
    const auto reset = [&]() {
        stack.clear(); available.clear(); ++region;
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
                // Prefer existing storage; bound key/cache memory.
                if (available.size() >= 4096) available.clear();
                available.insert_or_assign(key, Available{ins.operand, version, code.size()});
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
        const auto frequencyKey = std::to_string(region) + ":" + key;
        if (!materialize && contiguous && (counts.size() < 4096 || counts.contains(frequencyKey)))
            counts[frequencyKey] = std::min(2u, counts[frequencyKey] + 1);
        const auto prior = available.find(key);
        bool reused = false;
        if (materialize && contiguous && prior != available.end() &&
            versions.at(prior->second.slot) == prior->second.version && prior->second.definition < from) {
            // The value still resides in that local. Replacing only a
            // contiguous pure tree cannot erase intervening observable ops.
            const auto slot = prior->second.slot;
            // Parent reuse may erase newly memoized child definitions.
            // Invalidate those entries before later code can read them.
            for (auto it = available.begin(); it != available.end();) {
                if (it->second.definition >= from) it = available.erase(it);
                else ++it;
            }
            code.resize(from); lines.resize(from);
            append({Opcode::LocalGet, 0, slot}, line);
            reused = true;
        } else append(ins,line);
        const bool storedNext = n + 1 < fn.code.size() &&
            (fn.code[n + 1].opcode == Opcode::LocalSet || fn.code[n + 1].opcode == Opcode::LocalTee);
        const auto frequency = counts.find(frequencyKey);
        if (materialize && contiguous && !reused && !storedNext && start + 1 - from >= 3 &&
            frequency != counts.end() && frequency->second > 1 && versions.size() < kMaxLocals) {
            const auto slot = static_cast<u16>(versions.size());
            fn.locals.push_back(*signature.result);
            versions.push_back(1); bindings.push_back(key);
            if (available.size() >= 4096) available.clear();
            available.insert_or_assign(key, Available{slot, 1, code.size()});
            append({Opcode::LocalTee, 0, slot}, line);
        }
        if (signature.result) stack.push_back({key, contiguous ? from : start, code.size()});
    }
    if (materialize) { fn.code = std::move(code); fn.sourceLines = std::move(lines); }
}
} // namespace

void reuseCommonExpressions(Plan& plan) {
    for (auto& fn : plan.functions) {
        Counts counts;
        reuse(fn, counts, false);
        reuse(fn, counts, true);
    }
}
} // namespace iprop::compiler
