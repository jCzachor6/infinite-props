#include "iprop/Samplers.hpp"
#include "iprop/VmInternals.hpp"
#include "iprop/math/Distribution.hpp"

#include <string>

// Samplers and steppers capture the same endpoint/distribution data. Only
// their element generation differs (random draws versus unit positions).
namespace iprop::detail {
namespace {
const char* descriptionName(TypeDescriptor type) { return type.isStepper() ? "stepper" : "sampler"; }
} // namespace

TypeDescriptor descriptionType(Execution& ex, Instruction ins, TypeDescriptor::Kind kind) {
    if (ins.operand >= ex.file.collections.types.size())
        ex.trap("type descriptor index out of range", ErrorReason::IndexRange);
    const auto type = ex.file.collections.types[ins.operand];
    if (type.kind != kind || !type.isStorable())
        ex.trap(kind == TypeDescriptor::Kind::Stepper ? "expected stepper descriptor" : "expected sampler descriptor",
            ErrorReason::StackType);
    return type;
}

void retainDescriptionConstant(Execution& ex, const CapturedRangeData& description) {
    const auto& points = description.distribution.points;
    const bool retainDescription = !ex.resources.descriptionConstants.contains(&description);
    const bool retainPoints = points && !ex.resources.distributionConstants.contains(points.get());
    if (retainPoints) ex.spend(points->points.size());
    ex.allocateResource((retainDescription ? ResourceArena::descriptionBytes() : 0) +
        (retainPoints ? ResourceArena::distributionBytes(points->points.size()) : 0));
    if (retainDescription) ex.resources.descriptionConstants.insert(&description);
    if (retainPoints) ex.resources.distributionConstants.insert(points.get());
}

void captureDescription(Execution& ex, TypeDescriptor type, SamplerMode mode) {
    const std::string name = descriptionName(type);
    CapturedRangeData data;
    data.type = type;
    data.mode = mode;
    if (mode == SamplerMode::With) data.distribution = ex.popDistribution();
    // Endpoints keep their exact type; any other value is a stack type error.
    const auto bound = [&] {
        auto value = ex.pop();
        if (valueType(value) != type.item()) ex.trap(name + " bound type mismatch", ErrorReason::StackType);
        return value;
    };
    if (mode != SamplerMode::Constant) data.upper = bound();
    data.lower = bound();
    if (const auto* error = samplerBoundsError(type.item(), data.lower, data.upper, mode != SamplerMode::Constant))
        ex.trap(error, ErrorReason::NumericRange);
    if (mode == SamplerMode::With && !math::validDistribution(data.distribution))
        ex.trap(name + " distribution is not initialized or invalid", ErrorReason::NumericRange);
    ex.allocateResource(ResourceArena::descriptionBytes());
    auto frozen = std::make_shared<const CapturedRangeData>(std::move(data));
    ex.resources.descriptions.push_back(frozen);
    if (type.isStepper()) ex.push(StepperValue{type, std::move(frozen)});
    else ex.push(SamplerValue{type, std::move(frozen)});
}

std::shared_ptr<const CapturedRangeData> popDescription(Execution& ex, TypeDescriptor type) {
    const auto exact = [&](const auto& handle) {
        if (handle.type != type) ex.trap(std::string(descriptionName(type)) + " type mismatch", ErrorReason::StackType);
        return handle.data;
    };
    return type.isStepper() ? exact(ex.popHandle<StepperValue>("stepper handle"))
                            : exact(ex.popHandle<SamplerValue>("sampler handle"));
}

void pickDescriptionList(Execution& ex, TypeDescriptor type, DescriptionElement element) {
    // Both expressions have already run. Size is checked before the handle,
    // and all list storage is reserved before the first element.
    const auto count = ex.popI32();
    if (count < 0 || count > kMaxListElements)
        ex.trap(std::string(descriptionName(type)) + " result count must be between 0 and 256", ErrorReason::NumericRange);
    const auto description = popDescription(ex, type);
    ex.spend(static_cast<u64>(count));
    ex.allocateResource(ResourceArena::listBytes(static_cast<std::size_t>(count)));
    auto data = std::make_shared<ListData>();
    data->type = ValueType::list(type.item());
    data->elements.reserve(static_cast<std::size_t>(count));
    for (i32 i = 0; i < count; ++i) data->elements.push_back(element(ex, *description, i, count));
    // Publish only after every element and all resource checks succeed.
    ex.resources.lists.push_back(data);
    ex.push(ListValue{data->type, std::move(data)});
}

} // namespace iprop::detail
