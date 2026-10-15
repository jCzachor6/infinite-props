#include "iprop/Samplers.hpp"
#include "iprop/VmInternals.hpp"
#include "iprop/math/Color.hpp"
#include "iprop/math/Distribution.hpp"
#include "iprop/math/Vector.hpp"
#include <algorithm>

namespace iprop::detail {
namespace {
double position(Execution& ex, const StepperData& data, double lo, double hi, double t) {
    // Quantiles keep the sampler's failure rules and fuel order.
    if (data.mode == SamplerMode::With) return checkedTruncatedDraw(ex, data.distribution, lo, hi, t);
    if (t == 0.0) return lo;
    if (t == 1.0) return hi;
    if (lo == hi) return lo;
    // Convex binary64 interpolation keeps the entire i32 range exact at the
    // endpoints and avoids overflow for finite binary32 endpoints.
    return std::clamp(lo * (1.0 - t) + hi * t, lo, hi);
}

Value stepValue(Execution& ex, const StepperData& data, double t) {
    if (data.mode == SamplerMode::Constant) return data.lower;
    ex.spend(1);
    const auto item = data.type.item();
    if (item == ValueType::I32)
        return math::roundRangeInt(position(ex, data, std::get<i32>(data.lower), std::get<i32>(data.upper), t));
    if (item == ValueType::F32) {
        const auto lo = std::get<f32>(data.lower), hi = std::get<f32>(data.upper);
        return std::clamp(static_cast<f32>(position(ex, data, lo, hi, t)), lo, hi);
    }
    const auto coefficient = static_cast<f32>(position(ex, data, 0.0, 1.0, t));
    ex.spend(1); // The same interpolation work as a color/vec2 sampler.
    if (coefficient == 0.f) return data.lower;
    if (coefficient == 1.f) return data.upper;
    if (item == ValueType::Color)
        return math::colorMixRgba(std::get<Color>(data.lower), std::get<Color>(data.upper), coefficient);
    if (item == ValueType::Vec2)
        return math::vec2Mix(std::get<Vec2>(data.lower), std::get<Vec2>(data.upper), coefficient);
    ex.trap("unsupported stepper element type", ErrorReason::StackType);
}
} // namespace

bool stepSteppers(Execution& ex, Instruction ins) {
    constexpr auto kind = TypeDescriptor::Kind::Stepper;
    switch (ins.opcode) {
    case Opcode::ConstStepper: {
        if (ins.operand >= ex.file.collections.steppers.size())
            ex.trap("stepper constant index out of range", ErrorReason::IndexRange);
        const auto& stepper = ex.file.collections.steppers[ins.operand];
        retainDescriptionConstant(ex, *stepper.data);
        ex.push(stepper);
        break;
    }
    case Opcode::StepperValue: captureDescription(ex, descriptionType(ex, ins, kind), SamplerMode::Constant); break;
    case Opcode::StepperRange: captureDescription(ex, descriptionType(ex, ins, kind), SamplerMode::Uniform); break;
    case Opcode::StepperWith: captureDescription(ex, descriptionType(ex, ins, kind), SamplerMode::With); break;
    case Opcode::StepperPickList:
        pickDescriptionList(ex, descriptionType(ex, ins, kind), [](Execution& ex, const CapturedRangeData& stepper, i32 i, i32 count) {
            const double t = count == 1 ? 0.0 : static_cast<double>(i) / static_cast<double>(count - 1);
            return stepValue(ex, stepper, t);
        });
        break;
    default: return false;
    }
    ++ex.frame().ip;
    return true;
}
} // namespace iprop::detail
