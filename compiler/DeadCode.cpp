#include "Optimizer.hpp"
#include "iprop/OpcodeSpec.hpp"

#include <algorithm>
#include <bitset>
#include <deque>

namespace iprop::compiler {

std::optional<std::size_t> discardableInputs(Opcode op) {
    switch (op) {
    case Opcode::ConstI32: case Opcode::ConstF32: case Opcode::ConstColor:
    case Opcode::LocalGet: return 0;
    case Opcode::I32Add: case Opcode::I32Sub: case Opcode::I32Mul:
    case Opcode::I32And: case Opcode::I32Or: case Opcode::I32Xor:
    case Opcode::I32Shl: case Opcode::I32ShrS: case Opcode::I32ShrU:
    case Opcode::I32Neg: case Opcode::I32Not:
    case Opcode::I32Eq: case Opcode::I32Ne: case Opcode::I32LtS: case Opcode::I32LtU:
    case Opcode::I32LeS: case Opcode::I32GtS: case Opcode::I32GeS:
    case Opcode::F32Add: case Opcode::F32Sub: case Opcode::F32Mul: case Opcode::F32Div:
    case Opcode::F32Eq: case Opcode::F32Ne: case Opcode::F32Lt: case Opcode::F32Le:
    case Opcode::F32Gt: case Opcode::F32Ge: case Opcode::F32Neg: case Opcode::F32Abs:
    case Opcode::F32Sqrt: case Opcode::F32Sin: case Opcode::F32Cos:
    case Opcode::F32Floor: case Opcode::F32Ceil: case Opcode::F32Min: case Opcode::F32Max:
    case Opcode::F32Pow: case Opcode::F32Exp: case Opcode::F32Atan2:
    case Opcode::Hash: case Opcode::Smooth: case Opcode::Lerp:
    case Opcode::Gain: case Opcode::Quintic: case Opcode::Smoothstep:
    case Opcode::Remap: case Opcode::Clamp: case Opcode::Dither:
    case Opcode::I32ToF32: case Opcode::F32ToI32:
    case Opcode::ColorMix: case Opcode::ColorShade: case Opcode::ColorSetA:
    case Opcode::ColorMul: case Opcode::ColorScale:
    case Opcode::Vec2Make: case Opcode::Vec2Add: case Opcode::Vec2Sub:
    case Opcode::Vec2Mul: case Opcode::Vec2Div: case Opcode::Vec2Neg:
    case Opcode::Vec2X: case Opcode::Vec2Y:
    case Opcode::Vec2Len: case Opcode::Vec2Dot: case Opcode::Vec2Rot:
    case Opcode::Vec2Fold: case Opcode::Vec2Polar:
    case Opcode::DistCircle: case Opcode::DistRect: case Opcode::DistRoundRect:
    case Opcode::DistLine: case Opcode::DistBezier: case Opcode::DistBezierCubic:
    case Opcode::Fill:
        return findOpcode(op)->stack->inputCount;
    default: return std::nullopt;
    }
}

namespace {
using Live = std::bitset<kMaxLocals>;

// Resolve structured branches to instruction indices before editing any IR.
std::vector<std::vector<std::size_t>> successors(const std::vector<Instruction>& code) {
    const auto n = code.size();
    std::vector<std::size_t> ends(n, n), alternatives(n, n), targets(n, n), stack;
    for (std::size_t i = 0; i < n; ++i) {
        switch (code[i].opcode) {
        case Opcode::Block: case Opcode::Loop: case Opcode::If: stack.push_back(i); break;
        case Opcode::Else: alternatives[stack.back()] = i; targets[i] = stack.back(); break;
        case Opcode::End: ends[stack.back()] = i; stack.pop_back(); break;
        case Opcode::Br: case Opcode::BrIf: targets[i] = stack.at(stack.size() - 1 - code[i].operand); break;
        default: break;
        }
    }
    std::vector<std::vector<std::size_t>> next(n);
    for (std::size_t i = 0; i < n; ++i) {
        const auto op = code[i].opcode;
        if (op != Opcode::Br && op != Opcode::Else && op != Opcode::Return && i + 1 < n)
            next[i].push_back(i + 1);
        if (op == Opcode::If) {
            const auto target = alternatives[i] != n ? alternatives[i] + 1 : ends[i];
            if (target < n) next[i].push_back(target);
        } else if (op == Opcode::Br || op == Opcode::BrIf || op == Opcode::Else) {
            const auto owner = targets[i];
            const auto target = op != Opcode::Else && code[owner].opcode == Opcode::Loop ? owner + 1 : ends[owner];
            if (target < n) next[i].push_back(target);
        }
    }
    return next;
}

bool deadStores(PlannedFunction& f) {
    const auto n = f.code.size();
    const auto next = successors(f.code);
    std::vector<std::vector<std::size_t>> previous(n);
    for (std::size_t i = 0; i < n; ++i)
        for (const auto j : next[i]) previous[j].push_back(i);
    std::vector<Live> live(n), out(n);
    std::deque<std::size_t> work;
    std::vector<bool> queued(n, true);
    for (std::size_t i = n; i > 0; --i) work.push_back(i - 1);
    while (!work.empty()) {
        const auto i = work.front(); work.pop_front(); queued[i] = false;
        Live after;
        for (const auto j : next[i]) after |= live[j];
        out[i] = after;
        const auto ins = f.code[i];
        if (ins.opcode == Opcode::LocalSet || ins.opcode == Opcode::LocalTee) after.reset(ins.operand);
        if (ins.opcode == Opcode::LocalGet) after.set(ins.operand);
        if (after == live[i]) continue;
        live[i] = after;
        for (const auto j : previous[i]) if (!queued[j]) { work.push_back(j); queued[j] = true; }
    }
    bool changed = false;
    for (std::size_t i = 0; i < n; ++i) {
        auto& ins = f.code[i];
        if (ins.opcode == Opcode::LocalSet && !out[i].test(ins.operand)) {
            ins = {Opcode::Drop, 0, 0}; changed = true;
        } else if (ins.opcode == Opcode::LocalTee && !out[i].test(ins.operand)) {
            ins = {Opcode::Nop, 0, 0}; changed = true;
        }
    }
    return changed;
}

bool droppedValues(PlannedFunction& f) {
    std::vector<bool> remove(f.code.size());
    bool changed = false;
    for (std::size_t i = 0; i < f.code.size(); ++i) {
        if (f.code[i].opcode == Opcode::Nop) { remove[i] = true; changed = true; continue; }
        if (f.code[i].opcode != Opcode::Drop) continue;
        std::size_t need = 1, start = i;
        while (start > 0 && need != 0) {
            const auto inputs = discardableInputs(f.code[start - 1].opcode);
            if (remove[start - 1] || !inputs) break;
            --start;
            need = need - 1 + *inputs;
        }
        if (need != 0) continue; // helper/RNG/trap/stack boundary: retain evaluation
        std::fill(remove.begin() + static_cast<std::ptrdiff_t>(start), remove.begin() + static_cast<std::ptrdiff_t>(i + 1), true);
        changed = true;
    }
    if (!changed) return false;
    std::vector<Instruction> code;
    std::vector<u32> lines;
    for (std::size_t i = 0; i < f.code.size(); ++i) if (!remove[i]) {
        code.push_back(f.code[i]); lines.push_back(f.sourceLines[i]);
    }
    f.code = std::move(code); f.sourceLines = std::move(lines);
    return true;
}

void compactLocals(PlannedFunction& f) {
    const auto params = f.params.size();
    std::vector<bool> used(params + f.locals.size());
    for (const auto ins : f.code)
        if (ins.opcode == Opcode::LocalGet || ins.opcode == Opcode::LocalSet || ins.opcode == Opcode::LocalTee)
            used.at(ins.operand) = true;
    std::vector<u16> remap(used.size());
    std::vector<ValueType> locals;
    for (std::size_t i = 0; i < used.size(); ++i) {
        if (i < params) remap[i] = static_cast<u16>(i);
        else if (used[i]) {
            remap[i] = static_cast<u16>(params + locals.size()); locals.push_back(f.locals[i - params]);
        }
    }
    for (auto& ins : f.code)
        if (ins.opcode == Opcode::LocalGet || ins.opcode == Opcode::LocalSet || ins.opcode == Opcode::LocalTee)
            ins.operand = remap.at(ins.operand);
    for (auto it = f.slots.begin(); it != f.slots.end();) {
        if (it->second >= params && !used.at(it->second)) { f.langTypes.erase(it->first); it = f.slots.erase(it); }
        else { it->second = remap.at(it->second); ++it; }
    }
    f.locals = std::move(locals);
}
} // namespace

void eliminateDeadCode(Plan& plan) {
    for (auto& f : plan.functions) {
        // Each iteration may expose another dead definition. A bounded pass
        // is sufficient for safe optimisation even for adversarial chains.
        for (unsigned pass = 0; pass < 64; ++pass) {
            const bool stores = deadStores(f);
            const bool values = droppedValues(f);
            if (!stores && !values) break;
        }
        compactLocals(f);
    }
}
} // namespace iprop::compiler
