#include "iprop/PartRenderPlan.hpp"
#include "iprop/math/Float.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace iprop {
namespace {

i32 dimension(u32 size, f32 scale) {
    const f32 rounded = std::round(static_cast<f32>(size) * scale);
    if (size == 0 || !std::isfinite(scale) || scale <= 0.f ||
        !std::isfinite(rounded) || rounded > static_cast<f32>(kMaxCanvasSide)) {
        throw std::invalid_argument("part canvas too large or invalid");
    }
    return std::max(1, static_cast<i32>(rounded));
}

i32 coordinate(f32 value) {
    const f32 rounded = std::round(value);
    if (!std::isfinite(rounded) || rounded < -2147483648.f || rounded >= 2147483648.f) {
        throw std::invalid_argument("part coordinate out of range");
    }
    return static_cast<i32>(rounded);
}

} // namespace

PartRenderPlan PartRenderPlan::create(const Part& part, Vec2 callerScale,
                                     Vec2 placement, D4Transform transform) {
    PartRenderPlan plan;
    plan.transform = transform;
    plan.assetExtent = {static_cast<f32>(part.sizeX), static_cast<f32>(part.sizeY)};
    plan.sourceScale = transform.mapExtent(callerScale);
    plan.sourceExtent = {dimension(part.sizeX, plan.sourceScale.x),
                         dimension(part.sizeY, plan.sourceScale.y)};
    plan.targetExtent = transform.mapExtent(plan.sourceExtent);
    plan.targetOrigin = {coordinate(placement.x - part.origin.x * callerScale.x),
                         coordinate(placement.y - part.origin.y * callerScale.y)};
    plan.blitCost = static_cast<u64>(plan.sourceExtent.x) * static_cast<u64>(plan.sourceExtent.y);
    plan.callerOffset = {static_cast<f32>(plan.targetOrigin.x) / callerScale.x,
                         static_cast<f32>(plan.targetOrigin.y) / callerScale.y};
    return plan;
}

// One NaN encoding (spec v0.5 §6), independent of backend operand selection.
Vec2 PartRenderPlan::toCaller(Vec2 point) const {
    const Vec2 mapped = transform.mapPoint(point, assetExtent);
    return math::canonical(Vec2{mapped.x + callerOffset.x, mapped.y + callerOffset.y});
}

OutputRect PartRenderPlan::toCaller(OutputRect rect) const {
    const OutputRect mapped = transform.mapRect(rect, assetExtent);
    return {math::canonical(mapped.x + callerOffset.x), math::canonical(mapped.y + callerOffset.y),
            math::canonical(mapped.w), math::canonical(mapped.h)};
}

} // namespace iprop
