#include "iprop/Samplers.hpp"
#include "iprop/VmInternals.hpp"
#include "iprop/math/Distribution.hpp"
#include <cmath>

namespace iprop {
const char* samplerBoundsError(TypeDescriptor item, const Value& lower,
                               const Value& upper, bool range) {
    if (valueType(lower) != item || (range && valueType(upper) != item))
        return "sampler bound type mismatch";
    if (item == ValueType::I32) {
        if (range && std::get<i32>(lower) > std::get<i32>(upper))
            return "random range lower bound exceeds the upper bound";
    } else if (item == ValueType::F32) {
        if (!std::isfinite(std::get<f32>(lower)) || (range && !std::isfinite(std::get<f32>(upper))))
            return "random range bounds must be finite";
        if (range && std::get<f32>(lower) > std::get<f32>(upper))
            return "random range lower bound exceeds the upper bound";
    } else if (item == ValueType::Vec2) {
        const auto finite = [](Vec2 v) { return std::isfinite(v.x) && std::isfinite(v.y); };
        if (!finite(std::get<Vec2>(lower)) || (range && !finite(std::get<Vec2>(upper))))
            return "sampler vec2 bounds must be finite";
    } else if (item != ValueType::Color) return "unsupported sampler element type";
    return nullptr;
}
} // namespace iprop

namespace iprop::detail {
SamplerValue Execution::popSampler() {
    const auto value = pop();
    const auto* sampler = std::get_if<SamplerValue>(&value);
    if (!sampler) trap("stack type mismatch: expected sampler", ErrorReason::StackType);
    if (!sampler->data) trap("sampler handle is unassigned", ErrorReason::RuntimeViolation);
    return *sampler;
}

bool stepSamplers(Execution& ex, Instruction ins) {
    if (ins.opcode == Opcode::ConstSampler) {
        if (ins.operand >= ex.file.collections.samplers.size())
            ex.trap("sampler constant index out of range", ErrorReason::IndexRange);
        const auto& sampler = ex.file.collections.samplers[ins.operand];
        if (!ex.resources.samplerConstants.contains(sampler.data.get())) {
            ex.allocateResource(ResourceArena::samplerBytes());
            ex.resources.samplerConstants.insert(sampler.data.get());
        }
        ex.push(sampler); ++ex.frame().ip;
        return true;
    }
    switch (ins.opcode) {
    case Opcode::SamplerValue: case Opcode::SamplerRange: case Opcode::SamplerWith:
    case Opcode::SamplerPick: case Opcode::SamplerPickList: break;
    default: return false;
    }
    if (ins.operand >= ex.file.collections.types.size())
        ex.trap("type descriptor index out of range", ErrorReason::IndexRange);
    const auto type = ex.file.collections.types[ins.operand];
    if (!type.isSampler() || !type.isStorable()) ex.trap("expected sampler descriptor", ErrorReason::StackType);
    if (ins.opcode == Opcode::SamplerPickList) {
        // Both expressions have already run. Size is checked before the handle,
        // and all list storage is reserved before the first result draw.
        const auto count = ex.popI32();
        if (count < 0 || count > kMaxListElements)
            ex.trap("sampler result count must be between 0 and 256", ErrorReason::NumericRange);
        const auto sampler = ex.popSampler();
        if (sampler.type != type) ex.trap("sampler type mismatch", ErrorReason::StackType);
        ex.spend(static_cast<u64>(count));
        ex.allocateResource(ResourceArena::listBytes(static_cast<std::size_t>(count)));
        auto data = std::make_shared<ListData>();
        data->type = ValueType::list(type.item());
        data->elements.reserve(static_cast<std::size_t>(count));
        for (i32 i = 0; i < count; ++i) {
            if (sampler.data->mode != SamplerMode::Constant) ex.spend(1);
            data->elements.push_back(drawSampler(ex, *sampler.data));
        }
        ex.resources.lists.push_back(data);
        ex.push(ListValue{data->type, std::move(data)});
    } else if (ins.opcode == Opcode::SamplerPick) {
        const auto sampler = ex.popSampler();
        if (sampler.type != type) ex.trap("sampler type mismatch", ErrorReason::StackType);
        ex.push(drawSampler(ex, *sampler.data));
    } else {
        SamplerData data;
        data.type = type;
        data.mode = ins.opcode == Opcode::SamplerValue ? SamplerMode::Constant :
                    ins.opcode == Opcode::SamplerRange ? SamplerMode::Uniform : SamplerMode::With;
        if (data.mode == SamplerMode::With) data.distribution = ex.popDistribution();
        if (data.mode != SamplerMode::Constant) data.upper = ex.pop();
        data.lower = ex.pop();
        if (const auto* error = samplerBoundsError(type.item(), data.lower, data.upper,
                                                 data.mode != SamplerMode::Constant))
            ex.trap(error, ErrorReason::NumericRange);
        if (data.mode == SamplerMode::With && !math::validDistribution(data.distribution))
            ex.trap("sampler distribution is not initialized or invalid", ErrorReason::NumericRange);
        ex.allocateResource(ResourceArena::samplerBytes());
        auto frozen = std::make_shared<const SamplerData>(std::move(data));
        ex.resources.samplers.push_back(frozen);
        ex.push(SamplerValue{type, std::move(frozen)});
    }
    ++ex.frame().ip;
    return true;
}
} // namespace iprop::detail
