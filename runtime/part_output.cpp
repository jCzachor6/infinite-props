#include "iprop/PartOutput.hpp"
#include "iprop/VmInternals.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace iprop::detail {
namespace {
struct Point { double x, y; };
Point linear(D4Transform transform, Point p) {
    const auto a = transform.mapPixelWide({1,0}, {1,1});
    const auto b = transform.mapPixelWide({0,1}, {1,1});
    return {a.x*p.x+b.x*p.y, a.y*p.x+b.y*p.y};
}
i32 coordinate(Execution& ex, i64 value) {
    if (value < std::numeric_limits<i32>::min() || value > std::numeric_limits<i32>::max())
        ex.trap("part export translation is not representable", ErrorReason::NumericRange);
    return static_cast<i32>(value);
}
} // namespace

OutputPart snapshotPart(Execution& ex, const Part2dValue& part, u16 slot, u32 index) {
    if (!part.data) ex.trap("part2d is unassigned", ErrorReason::RuntimeViolation);
    const auto& p = *part.data;
    OutputPart result{part, p.transform, p.translation, p.original.scale,
        {ex.canvas().scaleX, ex.canvas().scaleY}, p.center, p.boundsOrigin, p.boundsSize, index};
    const bool geometry = std::holds_alternative<RegionSource>(p.source);
    if (geometry && !p.placed) result.sourceScale = result.targetScale;
    else if (p.transform.mapExtent(result.sourceScale) != result.targetScale)
        ex.trap("surface raster scale does not match the active target", ErrorReason::NumericRange);

    // Export is not a draw: appearance/texture scale and active clips do not
    // restrict a geometry-only snapshot. The source and appearance are exact.
    if (!ex.partCalls.empty()) {
        if (ex.slotOwner[slot] != ex.partCalls.back().partIndex)
            ex.trap("part export write by a different asset", ErrorReason::InvalidOutput);
        for (std::size_t i = ex.partCalls.size(); i-- > 0;) {
            ex.work();
            const auto& call = ex.partCalls[i];
            const auto& render = call.render;
            const auto translated = render.transform.mapPixelWide(
                {result.translation.x, result.translation.y}, render.sourceExtent);
            // Pixel-index D4 uses extent-1; affine edge coordinates use extent.
            const auto edgeCorrection = render.transform.mapPixelWide({0,0}, {2,2});
            result.translation = {
                coordinate(ex, translated.x + edgeCorrection.x + render.targetOrigin.x),
                coordinate(ex, translated.y + edgeCorrection.y + render.targetOrigin.y)};
            result.transform = result.transform.compose(render.transform);
            result.targetScale = {call.savedTarget.canvas().scaleX, call.savedTarget.canvas().scaleY};
        }
        const auto& f = p.original;
        const auto scaled = [&](double x, double y) {
            return linear(result.transform, {x/f.scale.x*result.sourceScale.x,
                                            y/f.scale.y*result.sourceScale.y});
        };
        const auto a = scaled(f.left, f.top), b = scaled(f.right, f.bottom);
        const auto c = scaled(f.centerX, f.centerY);
        result.center = {static_cast<f32>((c.x+result.translation.x)/result.targetScale.x),
                         static_cast<f32>((c.y+result.translation.y)/result.targetScale.y)};
        result.boundsOrigin = {static_cast<f32>((std::min(a.x,b.x)+result.translation.x)/result.targetScale.x),
                               static_cast<f32>((std::min(a.y,b.y)+result.translation.y)/result.targetScale.y)};
        result.boundsSize = {static_cast<f32>(std::abs(a.x-b.x)/result.targetScale.x),
                             static_cast<f32>(std::abs(a.y-b.y)/result.targetScale.y)};
    } else if (ex.slotOwner[slot] != kNoPartIndex) {
        ex.trap("part export write by a different asset", ErrorReason::InvalidOutput);
    }
    for (const auto value : {result.center.x, result.center.y, result.boundsOrigin.x,
            result.boundsOrigin.y, result.boundsSize.x, result.boundsSize.y})
        { ex.work(); if (!std::isfinite(value)) ex.trap("part export bounds are not representable", ErrorReason::NumericRange); }
    return result;
}
} // namespace iprop::detail
