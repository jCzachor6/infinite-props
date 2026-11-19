#include "iprop/TileCopies.hpp"
#include "iprop/VmInternals.hpp"
#include "iprop/Texture.hpp"
#include <cmath>
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

namespace {
std::shared_ptr<SurfaceBuffer> likeSurface(Execution& ex, const SurfaceBuffer& source, u64 width, u64 height) {
    if (width > kMaxCanvasSide + 2u * 256u || height > kMaxCanvasSide + 2u * 256u)
        ex.trap("generation data exceeds 64 MiB", ErrorReason::OutOfMemory);
    const bool mask = source.format == SurfaceBuffer::Format::A8;
    const auto w = static_cast<u32>(width), h = static_cast<u32>(height);
    const u64 bytes = mask ? maskBytes(w, h) : surfaceBytes(w, h);
    ex.spend(width * height);
    ex.allocateResource(bytes);
    auto buffer = ex.resources.makeSurface(bytes);
    buffer->format = source.format;
    buffer->encoding = source.encoding;
    buffer->raster.width = w; buffer->raster.height = h;
    buffer->raster.scaleX = source.raster.scaleX; buffer->raster.scaleY = source.raster.scaleY;
    const auto count = static_cast<std::size_t>(width * height);
    if (mask) initializeWork(buffer->coverage, count, u8{0}, *ex.workBudget);
    else initializeWork(buffer->raster.pixels, count, Color{}, *ex.workBudget);
    return buffer;
}

// Copies the `width` x `height` window of `source` whose first pixel is the
// source pixel (left, top), wrapped: one work step a row and one a pixel.
void copyWrapped(Execution& ex, const SurfaceBuffer& source, SurfaceBuffer& into, i64 left, i64 top) {
    const auto& from = source.raster;
    const u32 width = into.raster.width, height = into.raster.height;
    const bool mask = source.format == SurfaceBuffer::Format::A8;
    for (u32 y = 0; y < height; ++y) {
        ex.work();
        ex.workEach(width);
        const auto row = static_cast<std::size_t>(wrapIndex(top + y, from.height)) * from.width;
        const auto out = static_cast<std::size_t>(y) * width;
        for (u32 x = 0; x < width; ++x) {
            const auto at = row + wrapIndex(left + x, from.width);
            if (mask) into.coverage[out + x] = source.coverage[at];
            else into.raster.pixels[out + x] = from.pixels[at];
        }
    }
}
} // namespace

std::shared_ptr<const SurfaceBuffer> extendTiled(Execution& ex, const SurfaceBuffer& source, u32 margin) {
    auto wide = likeSurface(ex, source, static_cast<u64>(source.raster.width) + 2u * static_cast<u64>(margin),
        static_cast<u64>(source.raster.height) + 2u * static_cast<u64>(margin));
    copyWrapped(ex, source, *wide, -static_cast<i64>(margin), -static_cast<i64>(margin));
    return wide;
}

std::shared_ptr<const SurfaceBuffer> cropTiled(Execution& ex, const SurfaceBuffer& wide, u32 margin, u32 width, u32 height) {
    auto result = likeSurface(ex, wide, width, height);
    copyWrapped(ex, wide, *result, margin, margin);
    result->tile = true;
    return result;
}

u8 popOpacity(Execution& ex) {
    const f32 opacity = ex.popF32();
    if (!std::isfinite(opacity) || opacity < 0 || opacity > 1)
        ex.trap("paint opacity must be in 0..1", ErrorReason::NumericRange);
    return static_cast<u8>(std::floor(opacity * 255.f + 0.5f));
}

void compositePaintPixel(Execution& ex, i32 x, i32 y, Color src, CompositionMode mode) {
    if (!ex.target.clips.empty()) {
        const u8 clip = clipCoverage(ex, x, y);
        if (!clip) return;
        src.a = multiplyCoverage(src.a, clip);
    }
    auto& dst = ex.canvas().at(static_cast<u32>(x), static_cast<u32>(y));
    dst = composePaint(dst, src, resolveComposition(mode, ex.target.mode));
}

LayerValue Execution::popLayer() {
    auto image = popHandle<LayerValue>("layer");
    if (image.buffer->format != SurfaceBuffer::Format::RGBA8)
        trap("layer buffer must be RGBA8", ErrorReason::RuntimeViolation);
    return image;
}

namespace {
// `stored` maps a source pixel to the pixel that is painted: the identity for
// a colour buffer, the view's re-orientation for a direction buffer. It is
// fixed for the whole paint, so the colour instantiation is the loop it was.
// `Blended` selects the kernel once for the whole paint: the two original
// modes compose as they always did, a blend of the texture toolkit through
// composePaint.
template<bool Blended, class Stored>
void paintRows(Execution& ex, const LayerValue& image, CompositionMode mode, Stored stored) {
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
                const Color pixel = stored(source.at(sx, sy));
                if (pixel.a == 0 && mode == CompositionMode::Legacy) continue;
                const auto mapped = image.mapping.transform.mapPixel(
                    {static_cast<i32>(sx), static_cast<i32>(sy)}, extent);
                const i64 x = static_cast<i64>(image.mapping.origin.x) + mapped.x;
                const i64 y = static_cast<i64>(image.mapping.origin.y) + mapped.y;
                if (x < 0 || y < 0 || x >= target.width || y >= target.height) continue;
                Color& into = target.at(static_cast<u32>(x), static_cast<u32>(y));
                if constexpr (Blended) into = composePaint(into, pixel, mode);
                else into = composePixel(into, pixel, mode);
            }
            continue;
        }
        ex.work();
        for (u32 sx = 0; sx < source.width; ++sx) {
            ex.work();
            const Color pixel = stored(source.at(sx, sy));
            if (pixel.a == 0 && mode == CompositionMode::Legacy) continue;
            const auto mapped = image.mapping.transform.mapPixel(
                {static_cast<i32>(sx), static_cast<i32>(sy)}, extent);
            const i64 x = static_cast<i64>(image.mapping.origin.x) + mapped.x;
            const i64 y = static_cast<i64>(image.mapping.origin.y) + mapped.y;
            if (x >= 0 && y >= 0 && x < destination.width && y < destination.height) {
                if constexpr (Blended) compositePaintPixel(ex, static_cast<i32>(x), static_cast<i32>(y), pixel, mode);
                else compositePixel(ex, static_cast<i32>(x), static_cast<i32>(y), pixel, mode);
            }
        }
    }
}
} // namespace

// The caller reserves the whole source visit count before entering this loop.
// Shared by part returns and surface.paint, including transparent-pixel skips,
// placement rounding and D4's distinction between pixels and edge coordinates.
// A direction buffer seen through a D4 view has its vectors re-oriented too
// (spec normal maps v1.1); visits, fuel and work are those of any paint.
static void paintSurfaceCopy(Execution& ex, const LayerValue& image, CompositionMode mode, u8 opacity) {
    mode = resolveComposition(mode, ex.target.mode);
    const u8 kind = image.buffer->encoding == SurfaceBuffer::Encoding::Direction
        ? image.mapping.transform.kind() : u8{0};
    const bool blended = mode >= CompositionMode::Multiply;
    if (opacity != 255 || blended) {
        const auto faded = [=](Color pixel) {
            if (kind) pixel = reorientDirection(pixel, kind);
            pixel.a = multiplyCoverage(pixel.a, opacity);
            return pixel;
        };
        if (blended) paintRows<true>(ex, image, mode, faded);
        else paintRows<false>(ex, image, mode, faded);
        return;
    }
    if (kind == 0) paintRows<false>(ex, image, mode, [](const Color& pixel) { return pixel; });
    else paintRows<false>(ex, image, mode, [kind](const Color& pixel) { return reorientDirection(pixel, kind); });
}

void paintSurface(Execution& ex, const LayerValue& image, CompositionMode mode, u8 opacity) {
    if (!ex.target.tile) { paintSurfaceCopy(ex, image, mode, opacity); return; }
    const auto bounds = surfaceBounds(*image.buffer, image.mapping);
    tileCopies(ex, bounds.x, bounds.y, static_cast<double>(static_cast<i64>(bounds.x)+bounds.width-1),
        static_cast<double>(static_cast<i64>(bounds.y)+bounds.height-1), [&](i32 dx, i32 dy) {
            auto copy = image;
            if (dx || dy) ex.spend(image.buffer->raster.pixels.size());
            copy.mapping.origin.x += dx;
            copy.mapping.origin.y += dy;
            paintSurfaceCopy(ex, copy, mode, opacity);
        });
}

void Execution::beginLayer(u16 local, u8 aux) {
    if (!validLayerAux(aux)) trap("unknown layer alpha mode", ErrorReason::InvalidInstruction);
    const u8 mode = aux & kLayerModeMask;
    // Without either bit a capture tiles when the target it opens in does.
    const bool tile = (aux & kLayerTileOn) != 0 || ((aux & kLayerTileOff) == 0 && target.tile);
    // A direction capture composes source-over, whatever the active mode.
    const bool direction = mode == kDirectionLayerMode;
    const auto composition = direction ? CompositionMode::SourceOver
        : resolveComposition(static_cast<CompositionMode>(mode), target.mode);
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
        composition, workBudget.get());
    next.tile = next.buffer->tile = tile;
    if (direction) next.buffer->encoding = SurfaceBuffer::Encoding::Direction;
    LayerScope scope{frames.size(), frame().labels.size() + 1, local, std::move(target)};
    layerScopes.push_back(std::move(scope));
    target = std::move(next);
}

void Execution::closeOpenLayers(std::size_t labelDepth) {
    while (!layerScopes.empty() && layerScopes.back().frameDepth == frames.size() &&
           layerScopes.back().labelDepth > labelDepth) {
        auto scope = std::move(layerScopes.back());
        layerScopes.pop_back();
        if (scope.isExtension) extension->closeScope(*this);
        else if (scope.isClip) target.clips.resize(scope.savedClipDepth);
        else {
            frame().locals[scope.local] = target.view();
            target = std::move(scope.savedTarget);
        }
    }
}

bool stepSurfaces(Execution& ex, Instruction ins) {
    if (ins.opcode != Opcode::SurfacePaint) return false;
    if (!validCompositionMode(ins.operand)) ex.trap("unknown surface blend mode", ErrorReason::InvalidInstruction);
    priceBlendedPaint(ex, ins.operand);
    const auto image = ex.popLayer();
    const auto& source = image.buffer->raster;
    ex.spend(static_cast<u64>(source.width) * source.height);
    paintSurface(ex, image, static_cast<CompositionMode>(ins.operand));
    ++ex.frame().ip;
    return true;
}

} // namespace detail
} // namespace iprop
