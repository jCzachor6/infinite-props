#include "Optimizer.hpp"
#include <algorithm>

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
    std::vector<bool> types(old.typesValue().size()), lists(old.listsValue().size());
    const auto markType = [&](ValueType type) {
        if (!type.isParameterized()) return;
        const auto found = std::find(old.typesValue().begin(), old.typesValue().end(), type);
        if (found == old.typesValue().end()) throw std::logic_error("missing collection descriptor");
        types.at(static_cast<std::size_t>(found - old.typesValue().begin())) = true;
    };
    for (const auto& f : plan.functions) {
        for (const auto* slots : {&f.params, &f.results, &f.locals}) for (const auto type : *slots) markType(type);
        for (const auto ins : f.code) {
            if (ins.opcode == Opcode::ConstList) {
                lists.at(ins.operand) = true; markType(old.listsValue().at(ins.operand).type);
            }
            if (ins.opcode >= Opcode::ListBegin && ins.opcode <= Opcode::ListLen) types.at(ins.operand) = true;
            if (ins.opcode >= Opcode::SamplerValue && ins.opcode <= Opcode::SamplerPickList) types.at(ins.operand) = true;
        }
    }
    for (const auto& name : old.stringsValue()) if (names.contains(name)) pools.addString(name);
    const auto remap = [](const auto& values, const auto& used, auto add) {
        std::vector<u16> map(values.size());
        for (std::size_t i = 0; i < values.size(); ++i) if (used[i]) map[i] = static_cast<u16>(add(values[i]));
        return map;
    };
    const auto imap = remap(old.intsValue(), ints, [&](u32 x) { return pools.addInt(x); });
    const auto fmap = remap(old.floatsValue(), floats, [&](u32 x) { return pools.addFloat(x); });
    const auto cmap = remap(old.colorsValue(), colors, [&](Color x) { return pools.addColor(x); });
    const auto tmap = remap(old.typesValue(), types, [&](ValueType type) { return pools.addType(type); });
    const auto lmap = remap(old.listsValue(), lists, [&](const ListValue& list) { return pools.addList(list); });
    for (auto& f : plan.functions) for (auto& ins : f.code) {
        if (ins.opcode == Opcode::ConstList) ins.operand = lmap.at(ins.operand);
        if (ins.opcode >= Opcode::ListBegin && ins.opcode <= Opcode::ListLen) ins.operand = tmap.at(ins.operand);
        if (ins.opcode >= Opcode::SamplerValue && ins.opcode <= Opcode::SamplerPickList) ins.operand = tmap.at(ins.operand);
        if (ins.opcode == Opcode::ConstI32) ins.operand = imap.at(ins.operand);
        if (ins.opcode == Opcode::ConstF32) ins.operand = fmap.at(ins.operand);
        if (ins.opcode == Opcode::ConstColor) ins.operand = cmap.at(ins.operand);
    }
    plan.pools = std::move(pools);
}
} // namespace iprop::compiler
