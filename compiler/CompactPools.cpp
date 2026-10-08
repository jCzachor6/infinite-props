#include "Optimizer.hpp"

namespace iprop::compiler {

void compactPools(Plan& plan) {
    const Pools old = plan.pools;
    std::set<std::string> names;
    const auto metadata = [&](const ips::Program& p) {
        for (const auto& input : p.asset.inputs) names.insert(input.name);
        for (const auto& output : p.asset.outputs) names.insert(output.name);
    };
    metadata(*plan.entry);
    for (const auto* p : plan.parts) metadata(*p);
    for (const auto& instance : plan.instances) names.insert(instance.path);
    std::vector<bool> ints(old.intsValue().size()), floats(old.floatsValue().size()), colors(old.colorsValue().size());
    for (const auto& f : plan.functions) {
        names.insert(f.name);
        for (const auto& [name, slot] : f.slots) { (void)slot; names.insert(name); }
        for (const auto ins : f.code) {
            if (ins.opcode == Opcode::ConstI32) ints.at(ins.operand) = true;
            if (ins.opcode == Opcode::ConstF32) floats.at(ins.operand) = true;
            if (ins.opcode == Opcode::ConstColor) colors.at(ins.operand) = true;
        }
    }
    Pools pools;
    for (const auto& name : old.stringsValue()) if (names.contains(name)) pools.addString(name);
    const auto remap = [](const auto& values, const auto& used, auto add) {
        std::vector<u16> map(values.size());
        for (std::size_t i = 0; i < values.size(); ++i) if (used[i]) map[i] = static_cast<u16>(add(values[i]));
        return map;
    };
    const auto imap = remap(old.intsValue(), ints, [&](u32 x) { return pools.addInt(x); });
    const auto fmap = remap(old.floatsValue(), floats, [&](u32 x) { return pools.addFloat(x); });
    const auto cmap = remap(old.colorsValue(), colors, [&](Color x) { return pools.addColor(x); });
    for (auto& f : plan.functions) for (auto& ins : f.code) {
        if (ins.opcode == Opcode::ConstI32) ins.operand = imap.at(ins.operand);
        if (ins.opcode == Opcode::ConstF32) ins.operand = fmap.at(ins.operand);
        if (ins.opcode == Opcode::ConstColor) ins.operand = cmap.at(ins.operand);
    }
    plan.pools = std::move(pools);
}
} // namespace iprop::compiler
