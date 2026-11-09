#include "iprop/VmInternals.hpp"
#include <algorithm>

namespace iprop {

RenderTarget RenderTarget::create(u32 width, u32 height, Vec2 scale, CompositionMode mode, detail::WorkBudget* work) {
    return create(std::make_shared<SurfaceBuffer>(), width, height, scale, mode, work);
}

RenderTarget RenderTarget::create(std::shared_ptr<SurfaceBuffer> buffer,
        u32 width, u32 height, Vec2 scale, CompositionMode mode, detail::WorkBudget* work) {
    buffer->raster.resize(width, height, work);
    buffer->raster.scaleX = scale.x;
    buffer->raster.scaleY = scale.y;
    return {std::move(buffer), mode, {}};
}

ResourceArena::~ResourceArena() { clear(); }

std::shared_ptr<ResourceArena::SurfaceLifetime> ResourceArena::resourceLifetime() {
    if (!surfaceLifetime) surfaceLifetime = std::make_shared<SurfaceLifetime>(SurfaceLifetime{&bytes, {}, workBudget});
    return surfaceLifetime;
}

void ResourceArena::clear() {
    if (surfaceLifetime) surfaceLifetime->bytes = nullptr;
    surfaceLifetime.reset();
    lists.clear(); descriptions.clear(); constants.clear(); descriptionConstants.clear();
    distributions.clear(); distributionConstants.clear();
    bytes = 0;
}

std::shared_ptr<SurfaceBuffer> ResourceArena::makeSurface(u64 chargedBytes) {
    // The charge precedes this factory. Refund it even if allocation of the
    // lifetime token or the buffer itself fails before a deleter can own it.
    struct Refund {
        ResourceArena& arena;
        u64 bytes;
        ~Refund() { arena.release(bytes); }
    } refund{*this, chargedBytes};
    if (!surfaceLifetime) surfaceLifetime = std::make_shared<SurfaceLifetime>(SurfaceLifetime{&bytes, {}, workBudget});
    const std::weak_ptr<SurfaceLifetime> lifetime = surfaceLifetime;
    auto* raw = new SurfaceBuffer;
    refund.bytes = 0; // shared_ptr invokes the deleter if its control allocation fails
    auto buffer = std::shared_ptr<SurfaceBuffer>(raw,
        [lifetime, chargedBytes](SurfaceBuffer* value) {
            delete value;
            if (const auto live = lifetime.lock(); live && live->bytes)
                *live->bytes -= chargedBytes;
        });
    return buffer;
}

namespace detail {

LayerValue Execution::popLayer() {
    auto image = popHandle<LayerValue>("layer");
    if (image.buffer->format != SurfaceBuffer::Format::RGBA8)
        trap("layer buffer must be RGBA8", ErrorReason::RuntimeViolation);
    return image;
}

// The caller reserves the whole source visit count before entering this loop.
// Shared by part returns and surface.paint, including transparent-pixel skips,
// placement rounding and D4's distinction between pixels and edge coordinates.
void paintSurface(Execution& ex, const LayerValue& image, CompositionMode mode) {
    mode = resolveComposition(mode, ex.target.mode);
    const auto& source = image.buffer->raster;
    const auto& destination = ex.canvas();
    const RasterPoint extent{static_cast<i32>(source.width), static_cast<i32>(source.height)};
    const u64 rowSteps = 1u + static_cast<u64>(source.width);
    for (u32 sy = 0; sy < source.height; ++sy) {
        if (ex.prepayRow(rowSteps)) {
            // No clip: each written pixel is one composition into the target.
            PrepaidSteps polls{*ex.workBudget};
            Canvas& target = ex.canvas();
            for (u32 sx = 0; sx < source.width; ++sx) {
                polls.step();
                const Color pixel = source.at(sx, sy);
                if (pixel.a == 0 && mode == CompositionMode::Legacy) continue;
                const auto mapped = image.mapping.transform.mapPixel(
                    {static_cast<i32>(sx), static_cast<i32>(sy)}, extent);
                const i64 x = static_cast<i64>(image.mapping.origin.x) + mapped.x;
                const i64 y = static_cast<i64>(image.mapping.origin.y) + mapped.y;
                if (x < 0 || y < 0 || x >= target.width || y >= target.height) continue;
                Color& into = target.at(static_cast<u32>(x), static_cast<u32>(y));
                into = composePixel(into, pixel, mode);
            }
            continue;
        }
        ex.work();
        for (u32 sx = 0; sx < source.width; ++sx) {
            ex.work();
            const auto& pixel = source.at(sx, sy);
            if (pixel.a == 0 && mode == CompositionMode::Legacy) continue;
            const auto mapped = image.mapping.transform.mapPixel(
                {static_cast<i32>(sx), static_cast<i32>(sy)}, extent);
            const i64 x = static_cast<i64>(image.mapping.origin.x) + mapped.x;
            const i64 y = static_cast<i64>(image.mapping.origin.y) + mapped.y;
            if (x >= 0 && y >= 0 && x < destination.width && y < destination.height)
                compositePixel(ex, static_cast<i32>(x), static_cast<i32>(y), pixel, mode);
        }
    }
}

void Execution::beginLayer(u16 local, u8 mode) {
    if (!validCompositionMode(mode)) trap("unknown layer alpha mode", ErrorReason::InvalidInstruction);
    if (local < fn().params.size() || local >= frame().locals.size() ||
        valueType(frame().locals[local]) != ValueType::Layer)
        trap("layer destination must be a layer local", ErrorReason::StackType);
    if (layerScopes.size() >= kMaxSurfaceScopes)
        trap("surface scope depth exceeds 32", ErrorReason::RuntimeViolation);
    const auto width = canvas().width, height = canvas().height;
    const Vec2 scale{canvas().scaleX, canvas().scaleY};
    spend(fuelScaled(static_cast<u64>(width) * height, 70));
    allocateResource(surfaceBytes(width, height));
    // Like a part canvas, a capture starts without the parent's clips: they
    // apply exactly once, when the snapshot is painted.
    auto next = RenderTarget::create(resources.makeSurface(surfaceBytes(width, height)), width, height, scale,
        resolveComposition(static_cast<CompositionMode>(mode), target.mode), workBudget.get());
    LayerScope scope{frames.size(), frame().labels.size() + 1, local, std::move(target)};
    layerScopes.push_back(std::move(scope));
    target = std::move(next);
}

void Execution::closeOpenLayers(std::size_t labelDepth) {
    while (!layerScopes.empty() && layerScopes.back().frameDepth == frames.size() &&
           layerScopes.back().labelDepth > labelDepth) {
        auto scope = std::move(layerScopes.back());
        layerScopes.pop_back();
        if (scope.isClip) target.clips.resize(scope.savedClipDepth);
        else {
            frame().locals[scope.local] = target.view();
            target = std::move(scope.savedTarget);
        }
    }
}

bool stepSurfaces(Execution& ex, Instruction ins) {
    if (ins.opcode != Opcode::SurfacePaint) return false;
    if (!validCompositionMode(ins.operand)) ex.trap("unknown surface blend mode", ErrorReason::InvalidInstruction);
    const auto image = ex.popLayer();
    const auto& source = image.buffer->raster;
    ex.spend(static_cast<u64>(source.width) * source.height);
    paintSurface(ex, image, static_cast<CompositionMode>(ins.operand));
    ++ex.frame().ip;
    return true;
}

} // namespace detail
} // namespace iprop
