#include "iprop/Samplers.hpp"
#include "iprop/VmInternals.hpp"
#include "iprop/math/Color.hpp"
#include "iprop/math/Distribution.hpp"
#include "iprop/math/Vector.hpp"
#include <algorithm>

namespace iprop::detail {
namespace {
double quantile(Execution& ex, const Distribution& distribution, double lo, double hi, double t) {
    ex.spend(math::pointDrawWork(distribution, lo, hi));
    const auto result = math::truncatedDraw(distribution, lo, hi, t);
    ex.spend(result.steps);
    switch (result.status) {
    case math::DrawStatus::Outside:
        ex.trap("random range lies outside the distribution", ErrorReason::NumericRange);
    case math::DrawStatus::NoMass:
        ex.trap("random range has no probability under the distribution", ErrorReason::NumericRange);
    case math::DrawStatus::NoConvergence:
        ex.trap("distribution quantile did not converge", ErrorReason::NumericRange);
    case math::DrawStatus::Numerical:
        ex.trap("distribution quantile has a numerical error", ErrorReason::NumericRange);
    case math::DrawStatus::Ok: return result.value;
    }
    ex.trap("invalid distribution quantile status", ErrorReason::NumericRange);
}

double position(Execution& ex, const StepperData& data, double lo, double hi, double t) {
    if (data.mode == SamplerMode::With) return quantile(ex, data.distribution, lo, hi, t);
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
    if (ins.opcode == Opcode::ConstStepper) {
        if (ins.operand >= ex.file.collections.steppers.size())
            ex.trap("stepper constant index out of range", ErrorReason::IndexRange);
        const auto& stepper = ex.file.collections.steppers[ins.operand];
        const auto& points = stepper.data->distribution.points;
        const bool retainDescription = !ex.resources.stepperConstants.contains(stepper.data.get());
        const bool retainPoints = points && !ex.resources.distributionConstants.contains(points.get());
        if (retainPoints) ex.spend(points->points.size());
        ex.allocateResource((retainDescription ? ResourceArena::stepperBytes() : 0) +
            (retainPoints ? ResourceArena::distributionBytes(points->points.size()) : 0));
        if (retainDescription) ex.resources.stepperConstants.insert(stepper.data.get());
        if (retainPoints) ex.resources.distributionConstants.insert(points.get());
        ex.push(stepper); ++ex.frame().ip;
        return true;
    }
    switch (ins.opcode) {
    case Opcode::StepperValue: case Opcode::StepperRange: case Opcode::StepperWith:
    case Opcode::StepperPickList: break;
    default: return false;
    }
    if (ins.operand >= ex.file.collections.types.size())
        ex.trap("type descriptor index out of range", ErrorReason::IndexRange);
    const auto type = ex.file.collections.types[ins.operand];
    if (!type.isStepper() || !type.isStorable()) ex.trap("expected stepper descriptor", ErrorReason::StackType);
    if (ins.opcode == Opcode::StepperPickList) {
        const auto count = ex.popI32();
        if (count < 0 || count > kMaxListElements)
            ex.trap("stepper result count must be between 0 and 256", ErrorReason::NumericRange);
        const auto value = ex.pop();
        const auto* stepper = std::get_if<StepperValue>(&value);
        if (!stepper || stepper->type != type) ex.trap("stepper type mismatch", ErrorReason::StackType);
        if (!stepper->data) ex.trap("stepper handle is unassigned", ErrorReason::RuntimeViolation);
        ex.spend(static_cast<u64>(count));
        ex.allocateResource(ResourceArena::listBytes(static_cast<std::size_t>(count)));
        auto data = std::make_shared<ListData>();
        data->type = ValueType::list(type.item());
        data->elements.reserve(static_cast<std::size_t>(count));
        for (i32 i = 0; i < count; ++i) {
            const double t = count == 1 ? 0.0 : static_cast<double>(i) / static_cast<double>(count - 1);
            data->elements.push_back(stepValue(ex, *stepper->data, t));
        }
        // Publish only after every quantile and all resource checks succeed.
        ex.resources.lists.push_back(data);
        ex.push(ListValue{data->type, std::move(data)});
    } else {
        StepperData data;
        data.type = type;
        data.mode = ins.opcode == Opcode::StepperValue ? SamplerMode::Constant :
            ins.opcode == Opcode::StepperRange ? SamplerMode::Uniform : SamplerMode::With;
        if (data.mode == SamplerMode::With) data.distribution = ex.popDistribution();
        if (data.mode != SamplerMode::Constant) data.upper = ex.pop();
        data.lower = ex.pop();
        if (const auto* error = samplerBoundsError(type.item(), data.lower, data.upper, data.mode != SamplerMode::Constant))
            ex.trap(error, ErrorReason::NumericRange);
        if (data.mode == SamplerMode::With && !math::validDistribution(data.distribution))
            ex.trap("stepper distribution is not initialized or invalid", ErrorReason::NumericRange);
        ex.allocateResource(ResourceArena::stepperBytes());
        auto frozen = std::make_shared<const StepperData>(std::move(data));
        ex.resources.steppers.push_back(frozen);
        ex.push(StepperValue{type, std::move(frozen)});
    }
    ++ex.frame().ip;
    return true;
}
} // namespace iprop::detail
