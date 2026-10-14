#include "iprop/VmInternals.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace iprop {

MaskBounds maskBounds(const MaskValue& mask) {
    if (!mask.buffer) throw std::invalid_argument("mask is unassigned");
    const auto& raster = mask.buffer->raster;
    if (!raster.width || !raster.height || raster.width > 2147483647u || raster.height > 2147483647u)
        throw std::invalid_argument("mask extent is not representable");
    const auto extent = mask.mapping.transform.mapExtent(
        RasterPoint{static_cast<i32>(raster.width), static_cast<i32>(raster.height)});
    const auto origin = mask.mapping.origin;
    if (static_cast<i64>(origin.x) + extent.x - 1 > std::numeric_limits<i32>::max() ||
        static_cast<i64>(origin.y) + extent.y - 1 > std::numeric_limits<i32>::max())
        throw std::invalid_argument("mask mapping is not representable");
    return {origin.x, origin.y, static_cast<u32>(extent.x), static_cast<u32>(extent.y)};
}

u8 maskCoverage(const MaskValue& mask, i64 x, i64 y) {
    const auto& raster = mask.buffer->raster;
    const auto extent = mask.mapping.transform.mapExtent(
        RasterPoint{static_cast<i32>(raster.width), static_cast<i32>(raster.height)});
    x -= mask.mapping.origin.x;
    y -= mask.mapping.origin.y;
    if (x < 0 || y < 0 || x >= extent.x || y >= extent.y) return 0;
    const auto source = mask.mapping.transform.inverse().mapPixel(
        {static_cast<i32>(x), static_cast<i32>(y)}, extent);
    const auto index = static_cast<std::size_t>(source.y) * raster.width + static_cast<u32>(source.x);
    return mask.buffer->format == SurfaceBuffer::Format::A8 ?
        mask.buffer->coverage[index] : raster.pixels[index].a;
}

namespace detail {
namespace {

void validateMask(Execution& ex, const MaskValue& mask) {
    try { (void)maskBounds(mask); }
    catch (const std::invalid_argument& error) { ex.trap(error.what(), ErrorReason::NumericRange); }
    const auto& source = mask.buffer->raster;
    if (mask.buffer->format != SurfaceBuffer::Format::A8 && mask.buffer->format != SurfaceBuffer::Format::RGBA8)
        ex.trap("unknown mask buffer format", ErrorReason::RuntimeViolation);
    const auto scale = mask.mapping.transform.mapExtent(Vec2{source.scaleX, source.scaleY});
    if (!std::isfinite(scale.x) || !std::isfinite(scale.y) || scale.x <= 0 || scale.y <= 0 ||
        !std::isfinite(ex.canvas().scaleX) || !std::isfinite(ex.canvas().scaleY) ||
        scale.x != ex.canvas().scaleX || scale.y != ex.canvas().scaleY)
        ex.trap("mask raster scale does not match the active target", ErrorReason::NumericRange);
    const u64 count = static_cast<u64>(source.width) * source.height;
    if ((mask.buffer->format == SurfaceBuffer::Format::A8 && mask.buffer->coverage.size() != count) ||
        (mask.buffer->format == SurfaceBuffer::Format::RGBA8 && source.pixels.size() != count))
        ex.trap("mask buffer size does not match its extent", ErrorReason::RuntimeViolation);
}

MaskBounds boundsFromEdges(Execution& ex, i64 left, i64 top, i64 right, i64 bottom) {
    if (left < std::numeric_limits<i32>::min() || top < std::numeric_limits<i32>::min() ||
        right - 1 > std::numeric_limits<i32>::max() || bottom - 1 > std::numeric_limits<i32>::max() ||
        right <= left || bottom <= top || right - left > std::numeric_limits<i32>::max() ||
        bottom - top > std::numeric_limits<i32>::max())
        ex.trap("mask mapping is not representable", ErrorReason::NumericRange);
    return {static_cast<i32>(left), static_cast<i32>(top),
        static_cast<u32>(right - left), static_cast<u32>(bottom - top)};
}

std::shared_ptr<SurfaceBuffer> createMask(Execution& ex, const MaskBounds& bounds, u64 samples) {
    const u64 count = static_cast<u64>(bounds.width) * bounds.height;
    if (samples && count > std::numeric_limits<u64>::max() / samples)
        ex.trap("fuel exhausted", ErrorReason::FuelExhausted);
    // Both zeroing and the normative logical work are reserved before allocation.
    ex.spend(count);
    ex.spend(count * samples);
    ex.allocateResource(maskBytes(bounds.width, bounds.height));
    auto buffer = ex.resources.makeSurface(maskBytes(bounds.width, bounds.height));
    buffer->format = SurfaceBuffer::Format::A8;
    buffer->raster.width = bounds.width;
    buffer->raster.height = bounds.height;
    buffer->raster.scaleX = ex.canvas().scaleX;
    buffer->raster.scaleY = ex.canvas().scaleY;
    buffer->coverage.assign(static_cast<std::size_t>(count), 0);
    return buffer;
}

MaskValue algebra(Execution& ex, Opcode op, const MaskValue& a, const MaskValue* b) {
    const auto ab = maskBounds(a);
    auto bounds = ab;
    if (b) {
        const auto bb = maskBounds(*b);
        bounds = boundsFromEdges(ex, std::min<i64>(ab.x, bb.x), std::min<i64>(ab.y, bb.y),
            std::max(static_cast<i64>(ab.x) + ab.width, static_cast<i64>(bb.x) + bb.width),
            std::max(static_cast<i64>(ab.y) + ab.height, static_cast<i64>(bb.y) + bb.height));
    }
    auto buffer = createMask(ex, bounds, 1);
    for (u32 y = 0; y < bounds.height; ++y) for (u32 x = 0; x < bounds.width; ++x) {
        const i64 px = static_cast<i64>(bounds.x) + x, py = static_cast<i64>(bounds.y) + y;
        const auto av = maskCoverage(a, px, py), bv = b ? maskCoverage(*b, px, py) : u8{0};
        const auto coverage = op == Opcode::MaskUnion ? std::max(av, bv) :
            op == Opcode::MaskIntersect ? std::min(av, bv) :
            op == Opcode::MaskSubtract ? std::min(av, static_cast<u8>(255u - bv)) :
            static_cast<u8>(255u - av);
        buffer->coverage[static_cast<std::size_t>(y) * bounds.width + x] = coverage;
    }
    return {std::move(buffer), {{}, {bounds.x, bounds.y}}};
}

MaskValue morph(Execution& ex, const MaskValue& mask, i32 radius, bool expand) {
    if (radius < 0 || radius > kMaxMaskRadius)
        ex.trap("mask radius must be in 0..64", ErrorReason::NumericRange);
    auto bounds = maskBounds(mask);
    if (expand) bounds = boundsFromEdges(ex, static_cast<i64>(bounds.x) - radius,
        static_cast<i64>(bounds.y) - radius, static_cast<i64>(bounds.x) + bounds.width + radius,
        static_cast<i64>(bounds.y) + bounds.height + radius);
    // The disk needs no temporary heap allocation outside the arena. A
    // monotone integer boundary computes row extents and the logical count.
    std::array<i32, 2 * kMaxMaskRadius + 1> halfWidths{};
    u64 samples = 0;
    i32 horizontal = radius;
    for (i32 dy = 0; dy <= radius; ++dy) {
        while (horizontal * horizontal + dy * dy > radius * radius) --horizontal;
        halfWidths[static_cast<std::size_t>(radius + dy)] = horizontal;
        halfWidths[static_cast<std::size_t>(radius - dy)] = horizontal;
        samples += static_cast<u64>(2 * horizontal + 1) * (dy ? 2u : 1u);
    }
    auto buffer = createMask(ex, bounds, samples);
    for (u32 y = 0; y < bounds.height; ++y) for (u32 x = 0; x < bounds.width; ++x) {
        u8 result = expand ? 0 : 255;
        const u8 terminal = expand ? 255 : 0;
        for (i32 dy = -radius; dy <= radius && result != terminal; ++dy) {
            const auto half = halfWidths[static_cast<std::size_t>(radius + dy)];
            for (i32 dx = -half; dx <= half && result != terminal; ++dx) {
                const auto value = maskCoverage(mask, static_cast<i64>(bounds.x) + x + dx,
                    static_cast<i64>(bounds.y) + y + dy);
                result = expand ? std::max(result, value) : std::min(result, value);
            }
        }
        buffer->coverage[static_cast<std::size_t>(y) * bounds.width + x] = result;
    }
    return {std::move(buffer), {{}, {bounds.x, bounds.y}}};
}

} // namespace

MaskValue Execution::popMask() {
    auto value = pop();
    const auto* mask = std::get_if<MaskValue>(&value);
    if (!mask) trap("stack type mismatch: expected mask", ErrorReason::StackType);
    if (!mask->buffer) trap("mask is unassigned", ErrorReason::RuntimeViolation);
    validateMask(*this, *mask);
    return *mask;
}

void Execution::beginClip() {
    const auto mask = popMask();
    if (layerScopes.size() >= kMaxSurfaceScopes)
        trap("surface scope depth exceeds 32", ErrorReason::RuntimeViolation);
    LayerScope scope;
    scope.frameDepth = frames.size();
    scope.labelDepth = frame().labels.size() + 1;
    scope.isClip = true;
    scope.savedClipDepth = target.clips.size();
    layerScopes.push_back(std::move(scope));
    target.clips.push_back(mask);
}

u8 clipCoverage(Execution& ex, i32 x, i32 y) {
    ex.spend(ex.target.clips.size());
    u8 coverage = 255;
    for (const auto& mask : ex.target.clips)
        coverage = multiplyCoverage(coverage, maskCoverage(mask, x, y));
    return coverage;
}

void weatherPixel(Execution& ex, i32 x, i32 y, Color proposed) {
    if (ex.target.clips.empty()) {
        ex.canvas().at(static_cast<u32>(x), static_cast<u32>(y)) = proposed;
        return;
    }
    const auto coverage = clipCoverage(ex, x, y);
    if (!coverage) return;
    auto& old = ex.canvas().at(static_cast<u32>(x), static_cast<u32>(y));
    const auto mix = [coverage](u8 a, u8 b) {
        return static_cast<u8>((static_cast<u32>(a) * (255u - coverage) +
            static_cast<u32>(b) * coverage + 127u) / 255u);
    };
    old = {mix(old.r, proposed.r), mix(old.g, proposed.g), mix(old.b, proposed.b), mix(old.a, proposed.a)};
}

bool stepMasks(Execution& ex, Instruction ins) {
    switch (ins.opcode) {
    case Opcode::MaskFrom: {
        const auto image = ex.popLayer();
        MaskValue mask{image.buffer, image.mapping};
        validateMask(ex, mask);
        ex.push(std::move(mask));
        break;
    }
    case Opcode::MaskUnion: case Opcode::MaskIntersect: case Opcode::MaskSubtract: {
        const auto b = ex.popMask(), a = ex.popMask();
        ex.push(algebra(ex, ins.opcode, a, &b));
        break;
    }
    case Opcode::MaskInvert: {
        const auto mask = ex.popMask();
        ex.push(algebra(ex, ins.opcode, mask, nullptr));
        break;
    }
    case Opcode::MaskExpand: case Opcode::MaskShrink: {
        const auto radius = ex.popI32();
        const auto mask = ex.popMask();
        ex.push(morph(ex, mask, radius, ins.opcode == Opcode::MaskExpand));
        break;
    }
    case Opcode::MaskOffset: {
        const auto dy = ex.popI32(), dx = ex.popI32();
        auto mask = ex.popMask();
        const auto bounds = maskBounds(mask);
        const auto shifted = boundsFromEdges(ex, static_cast<i64>(bounds.x) + dx,
            static_cast<i64>(bounds.y) + dy, static_cast<i64>(bounds.x) + bounds.width + dx,
            static_cast<i64>(bounds.y) + bounds.height + dy);
        mask.mapping.origin = {shifted.x, shifted.y};
        ex.push(std::move(mask));
        break;
    }
    case Opcode::SurfacePaintMask: {
        if (!validCompositionMode(ins.operand)) ex.trap("unknown surface blend mode", ErrorReason::InvalidInstruction);
        const auto color = ex.popColor();
        const auto mask = ex.popMask();
        const auto bounds = maskBounds(mask);
        ex.spend(static_cast<u64>(bounds.width) * bounds.height);
        for (u32 y = 0; y < bounds.height; ++y) for (u32 x = 0; x < bounds.width; ++x) {
            const i64 px = static_cast<i64>(bounds.x) + x, py = static_cast<i64>(bounds.y) + y;
            const auto coverage = maskCoverage(mask, px, py);
            // Zero mask coverage never touches hidden RGB, even in source-over.
            if (!coverage || !ex.canvas().contains(static_cast<i32>(px), static_cast<i32>(py))) continue;
            auto source = color;
            source.a = multiplyCoverage(source.a, coverage);
            compositePixel(ex, static_cast<i32>(px), static_cast<i32>(py), source,
                static_cast<CompositionMode>(ins.operand));
        }
        break;
    }
    default: return false;
    }
    ++ex.frame().ip;
    return true;
}

} // namespace detail
} // namespace iprop
