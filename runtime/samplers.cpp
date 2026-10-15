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
bool stepSamplers(Execution& ex, Instruction ins) {
    constexpr auto kind = TypeDescriptor::Kind::Sampler;
    switch (ins.opcode) {
    case Opcode::ConstSampler: {
        if (ins.operand >= ex.file.collections.samplers.size())
            ex.trap("sampler constant index out of range", ErrorReason::IndexRange);
        const auto& sampler = ex.file.collections.samplers[ins.operand];
        retainDescriptionConstant(ex, *sampler.data);
        ex.push(sampler);
        break;
    }
    case Opcode::SamplerValue: captureDescription(ex, descriptionType(ex, ins, kind), SamplerMode::Constant); break;
    case Opcode::SamplerRange: captureDescription(ex, descriptionType(ex, ins, kind), SamplerMode::Uniform); break;
    case Opcode::SamplerWith: captureDescription(ex, descriptionType(ex, ins, kind), SamplerMode::With); break;
    case Opcode::SamplerPick: {
        const auto sampler = popDescription(ex, descriptionType(ex, ins, kind));
        ex.push(drawSampler(ex, *sampler));
        break;
    }
    case Opcode::SamplerPickList:
        // A batch charges one more fuel per nonconstant draw than a single pick.
        pickDescriptionList(ex, descriptionType(ex, ins, kind), [](Execution& ex, const CapturedRangeData& sampler, i32, i32) {
            if (sampler.mode != SamplerMode::Constant) ex.spend(1);
            return drawSampler(ex, sampler);
        });
        break;
    default: return false;
    }
    ++ex.frame().ip;
    return true;
}
} // namespace iprop::detail
