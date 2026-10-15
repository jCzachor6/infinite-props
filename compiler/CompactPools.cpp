#include "Optimizer.hpp"
#include <algorithm>
#include <array>

namespace iprop::compiler {
namespace {

enum Pool : std::size_t { Ints, Floats, Colors, Types, Lists, Samplers, Steppers, Distributions, PoolCount };

// The constant pool an instruction operand indexes, from its opcode's
// operand kind: one classification drives both marking and remapping.
std::optional<Pool> operandPool(Opcode opcode) {
    switch (operandKind(opcode)) {
    case OperandKind::IntConst: return Ints;
    case OperandKind::FloatConst: return Floats;
    case OperandKind::ColorConst: return Colors;
    case OperandKind::TypeDescriptor: return Types;
    case OperandKind::ListConst: return Lists;
    case OperandKind::SamplerConst: return Samplers;
    case OperandKind::StepperConst: return Steppers;
    case OperandKind::DistributionConst: return Distributions;
    default: return std::nullopt;
    }
}

} // namespace

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
    std::array<std::vector<bool>, PoolCount> used;
    used[Ints].resize(old.intsValue().size());
    used[Floats].resize(old.floatsValue().size());
    used[Colors].resize(old.colorsValue().size());
    used[Types].resize(old.typesValue().size());
    used[Lists].resize(old.listsValue().size());
    used[Samplers].resize(old.samplersValue().size());
    used[Steppers].resize(old.steppersValue().size());
    used[Distributions].resize(old.distributionsValue().size());
    const auto markType = [&](ValueType type) {
        if (type.needsDescriptor()) used[Types].at(old.typeIndexOf(type)) = true;
    };
    for (const auto& output : plan.resourceExports) markType(output.type);
    for (const auto& f : plan.functions) {
        names.insert(f.name);
        for (const auto& [name, slot] : f.slots) { (void)slot; names.insert(name); }
        for (const auto* slots : {&f.params, &f.results, &f.locals}) for (const auto type : *slots) markType(type);
        for (const auto ins : f.code)
            if (const auto pool = operandPool(ins.opcode)) used[*pool].at(ins.operand) = true;
    }
    // Constants read by instructions keep their own descriptors.
    for (std::size_t i = 0; i < used[Lists].size(); ++i) if (used[Lists][i]) markType(old.listsValue()[i].type);
    for (std::size_t i = 0; i < used[Samplers].size(); ++i) if (used[Samplers][i]) markType(old.samplersValue()[i].type);
    for (std::size_t i = 0; i < used[Steppers].size(); ++i) if (used[Steppers][i]) markType(old.steppersValue()[i].type);

    Pools pools;
    for (const auto& name : old.stringsValue()) if (names.contains(name)) pools.addString(name);
    std::array<std::vector<u16>, PoolCount> maps;
    const auto remap = [&](Pool pool, const auto& values, auto add) {
        maps[pool].resize(values.size());
        for (std::size_t i = 0; i < values.size(); ++i) if (used[pool][i]) maps[pool][i] = static_cast<u16>(add(values[i]));
    };
    remap(Ints, old.intsValue(), [&](u32 x) { return pools.addInt(x); });
    remap(Floats, old.floatsValue(), [&](u32 x) { return pools.addFloat(x); });
    remap(Colors, old.colorsValue(), [&](Color x) { return pools.addColor(x); });
    remap(Types, old.typesValue(), [&](ValueType type) { return pools.addType(type); });
    // Preserve used sampler order, including dependencies of list constants.
    for (std::size_t i = 0; i < used[Lists].size(); ++i) if (used[Lists][i] && old.listsValue()[i].type.item().isSampler())
        for (const auto& value : old.listsValue()[i].data->elements) {
            const auto& sampler = std::get<SamplerValue>(value);
            const auto found = std::find_if(old.samplersValue().begin(), old.samplersValue().end(),
                [&](const auto& s) { return s.data == sampler.data; });
            used[Samplers].at(static_cast<std::size_t>(found - old.samplersValue().begin())) = true;
        }
    for (std::size_t i = 0; i < used[Lists].size(); ++i) if (used[Lists][i] && old.listsValue()[i].type.item().isStepper())
        for (const auto& value : old.listsValue()[i].data->elements) {
            const auto& stepper = std::get<StepperValue>(value);
            const auto found = std::find_if(old.steppersValue().begin(), old.steppersValue().end(),
                [&](const auto& s) { return s.data == stepper.data; });
            used[Steppers].at(static_cast<std::size_t>(found - old.steppersValue().begin())) = true;
        }
    const auto markDistribution = [&](const Distribution& d) {
        if (!d.points) return;
        const auto found = std::find_if(old.distributionsValue().begin(), old.distributionsValue().end(),
            [&](const auto& candidate) { return candidate.points == d.points; });
        if (found == old.distributionsValue().end()) throw std::logic_error("missing distribution constant");
        used[Distributions].at(static_cast<std::size_t>(found - old.distributionsValue().begin())) = true;
    };
    for (std::size_t i = 0; i < used[Samplers].size(); ++i) if (used[Samplers][i]) markDistribution(old.samplersValue()[i].data->distribution);
    for (std::size_t i = 0; i < used[Steppers].size(); ++i) if (used[Steppers][i]) markDistribution(old.steppersValue()[i].data->distribution);
    for (std::size_t i = 0; i < used[Lists].size(); ++i) if (used[Lists][i] && old.listsValue()[i].type.item() == ValueType::Distribution)
        for (const auto& value : old.listsValue()[i].data->elements) markDistribution(std::get<Distribution>(value));
    remap(Distributions, old.distributionsValue(), [&](const Distribution& d) { return pools.addDistribution(d); });
    remap(Samplers, old.samplersValue(), [&](const SamplerValue& sampler) { return pools.addSampler(sampler); });
    remap(Steppers, old.steppersValue(), [&](const StepperValue& stepper) { return pools.addStepper(stepper); });
    remap(Lists, old.listsValue(), [&](const ListValue& list) { return pools.addList(list); });
    for (auto& f : plan.functions) for (auto& ins : f.code)
        if (const auto pool = operandPool(ins.opcode)) ins.operand = maps[*pool].at(ins.operand);
    plan.pools = std::move(pools);
}
} // namespace iprop::compiler
