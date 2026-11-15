#include "iprop/NormalMap.hpp"

#include "iprop/ResourceMemory.hpp"
#include "iprop/VmInternals.hpp"
#include "iprop/math/Distribution.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>

// Normal maps (spec normal maps v1.1): the height-to-direction kernel, the
// publication of the root's normal output and the derived fallback. All
// arithmetic is binary64 in the written order; the build forbids contraction.

namespace iprop {

bool validDerivedNormalRequest(const DerivedNormalRequest& request) noexcept {
    return std::isfinite(request.depth) && request.depth >= 0.f &&
        std::isfinite(request.rounding) && request.rounding >= 0.f && request.rounding <= kMaxNormalSmoothing &&
        std::isfinite(request.detail) && request.detail >= 0.f && request.detail <= 1.f;
}

namespace detail {

u8 directionByte(double value) {
    // Nearest stored value, ties away from zero, so a component and its
    // negation are exact complements. Zero (and a NaN, which no valid input
    // produces) is 128.
    const bool negative = value < 0.0;
    const double magnitude = negative ? -value : value;
    if (!(magnitude == magnitude)) return 128;
    const double scaled = 128.0 + 127.5 * magnitude;
    const u8 byte = scaled < 255.0 ? static_cast<u8>(std::floor(scaled)) : u8{255};
    return negative ? static_cast<u8>(255u - byte) : byte;
}

namespace {

constexpr i32 kMaxSmoothingRadius = 192;

i32 smoothingRadius(f32 sigma) {
    return sigma > 0.f ? static_cast<i32>(std::ceil(3.0 * static_cast<double>(sigma))) : 0;
}

// The rows a vertical pass keeps besides the plane: the r rows above the one
// being written, as they were before the pass.
u64 smoothingRingBytes(u32 width, i32 radius) {
    return static_cast<u64>(width) * static_cast<u64>(radius) * sizeof(double);
}

// The Gaussian of blur (spec §35) applied in place to one binary64 plane:
// the same normalized weights, horizontal then vertical, every sum
// accumulated from zero in increasing offset order, nearest-edge repetition
// and no intermediate rounding. Work: one step per weight and one per
// normalization, then per pass one step per row and one per tap of every
// pixel. One uncharged row of doubles holds the row being replaced.
void smoothHeightPlane(std::vector<double>& plane, u32 width, u32 height, f32 sigma, WorkBudget& work) {
    const i32 radius = smoothingRadius(sigma);
    const u32 kernel = static_cast<u32>(2 * radius + 1);
    std::array<double, 2 * kMaxSmoothingRadius + 1> weights{};
    const double denominator = 2.0 * static_cast<double>(sigma) * static_cast<double>(sigma);
    double total = 0.0;
    for (i32 j = -radius; j <= radius; ++j) {
        work.spend();
        const auto index = static_cast<std::size_t>(j + radius);
        weights[index] = math::pinnedExp(-static_cast<double>(j * j) / denominator);
        total = total + weights[index];
    }
    for (u32 j = 0; j < kernel; ++j) { work.spend(); weights[j] = weights[j] / total; }

    const std::size_t columns = width;
    const std::unique_ptr<double[]> line(new double[columns]);
    const u64 rowTaps = static_cast<u64>(width) * kernel;
    const auto clampIndex = [](i64 value, u32 extent) {
        return static_cast<std::size_t>(std::clamp(value, i64{0}, static_cast<i64>(extent) - 1));
    };

    for (u32 y = 0; y < height; ++y) {
        work.spend();
        work.spendEach(rowTaps);
        double* const row = plane.data() + static_cast<std::size_t>(y) * columns;
        std::copy(row, row + columns, line.get());
        std::fill(row, row + columns, 0.0);
        for (i32 j = -radius; j <= radius; ++j) {
            const double weight = weights[static_cast<std::size_t>(j + radius)];
            for (u32 x = 0; x < width; ++x)
                row[x] = row[x] + line[clampIndex(static_cast<i64>(x) + j, width)] * weight;
        }
    }

    // Rows at or below the one being written still hold their horizontal
    // sums in the plane; the rows above it are read from the ring.
    const auto ringRows = static_cast<std::size_t>(radius);
    std::vector<double> ring;
    initializeWork(ring, ringRows * columns, 0.0, work);
    for (u32 y = 0; y < height; ++y) {
        work.spend();
        work.spendEach(rowTaps);
        double* const sums = line.get();
        std::fill(sums, sums + columns, 0.0);
        for (i32 j = -radius; j <= radius; ++j) {
            const auto sy = clampIndex(static_cast<i64>(y) + j, height);
            const double* const from = sy < y ? ring.data() + (sy % ringRows) * columns
                                              : plane.data() + sy * columns;
            const double weight = weights[static_cast<std::size_t>(j + radius)];
            for (std::size_t x = 0; x < columns; ++x) sums[x] = sums[x] + from[x] * weight;
        }
        double* const row = plane.data() + static_cast<std::size_t>(y) * columns;
        std::copy(row, row + columns, ring.data() + (static_cast<std::size_t>(y) % ringRows) * columns);
        std::copy(sums, sums + columns, row);
    }
}

// Kernel steps 3-4: central differences with nearest-edge repetition, then
// the direction bytes. `alpha(index)` is the stored alpha of a pixel. Work:
// one step per row and four per pixel.
template<class Alpha>
void encodeDirections(const std::vector<double>& plane, u32 width, u32 height,
                      double k, Alpha alpha, std::vector<Color>& out, WorkBudget& work) {
    const std::size_t columns = width;
    for (u32 y = 0; y < height; ++y) {
        work.spend();
        work.spendEach(static_cast<u64>(width) * 4u);
        const double* const row = plane.data() + static_cast<std::size_t>(y) * columns;
        const double* const above = plane.data() + static_cast<std::size_t>(y == 0 ? 0 : y - 1) * columns;
        const double* const below = plane.data() + static_cast<std::size_t>(y + 1 == height ? y : y + 1) * columns;
        for (u32 x = 0; x < width; ++x) {
            const double gx = (row[x + 1 == width ? x : x + 1] - row[x == 0 ? 0 : x - 1]) * 0.5;
            const double gy = (below[x] - above[x]) * 0.5;
            const double vx = -(gx * k);
            const double vy = gy * k;
            const double length = std::sqrt((vx * vx + vy * vy) + 1.0);
            const auto index = static_cast<std::size_t>(y) * columns + x;
            out[index] = Color{directionByte(vx / length), directionByte(vy / length),
                               directionByte(1.0 / length), alpha(index)};
        }
    }
}

// surface.normal: a new direction buffer with the receiver's extent and scale.
std::shared_ptr<const SurfaceBuffer> heightDirections(Execution& ex, const SurfaceBuffer& source, bool mask,
                                                      f32 depth, f32 smoothing) {
    const auto& raster = source.raster;
    const u32 width = raster.width, height = raster.height;
    const u64 pixels = static_cast<u64>(width) * height;
    const i32 radius = smoothingRadius(smoothing);
    const u64 kernel = radius ? static_cast<u64>(2 * radius + 1) : 0u;
    ex.spend(kernel + pixels * (5u + 2u * kernel));
    const u64 resultBytes = surfaceBytes(width, height);
    const u64 scratchBytes = pixels * sizeof(double) + smoothingRingBytes(width, radius);
    if (!ex.resources.canAllocate(resultBytes + scratchBytes))
        ex.trap("generation data exceeds 64 MiB", ErrorReason::OutOfMemory);
    const ResourceReservation scratch(ex.resources, scratchBytes);
    ex.allocateResource(resultBytes);
    auto result = ex.resources.makeSurface(resultBytes);
    result->format = SurfaceBuffer::Format::RGBA8;
    result->encoding = SurfaceBuffer::Encoding::Direction;
    result->raster.width = width; result->raster.height = height;
    result->raster.scaleX = raster.scaleX; result->raster.scaleY = raster.scaleY;
    WorkBudget& work = *ex.workBudget;
    work.location = ex.activeInstruction;
    initializeWork(result->raster.pixels, static_cast<std::size_t>(pixels), Color{}, work);
    std::vector<double> plane;
    initializeWork(plane, static_cast<std::size_t>(pixels), 0.0, work);

    const bool coverage = source.format == SurfaceBuffer::Format::A8;
    const auto alphaAt = [&](std::size_t index) {
        return coverage ? source.coverage[index] : raster.pixels[index].a;
    };
    for (u32 y = 0; y < height; ++y) {
        work.spendEach(width);
        const auto first = static_cast<std::size_t>(y) * width;
        for (u32 x = 0; x < width; ++x) {
            const auto index = first + x;
            if (mask) plane[index] = static_cast<double>(alphaAt(index));
            else {
                const auto pixel = raster.pixels[index];
                plane[index] = (static_cast<double>(pixel.r) * static_cast<double>(pixel.a)) / 255.0;
            }
        }
    }
    if (radius) smoothHeightPlane(plane, width, height, smoothing, work);
    // Depth and smoothing are in the buffer's own pixels, as blur's sigma is:
    // the result depends on the stored bytes alone, never on the raster scale.
    const double k = static_cast<double>(depth) / 255.0;
    encodeDirections(plane, width, height, k, alphaAt, result->raster.pixels, work);
    return result;
}

} // namespace

Canvas deriveNormalImage(const Canvas& image, const DerivedNormalRequest& request, WorkBudget& work) {
    const u32 width = image.width, height = image.height;
    const u64 pixels = static_cast<u64>(width) * height;
    // Zero selects each default. Rounding and depth follow the image's longer
    // side, so a rounded edge keeps its slope at every size; half the height
    // comes from brightness, the only relief of a fully opaque image.
    const f32 longer = static_cast<f32>(std::max(width, height));
    const f32 rounding = request.rounding != 0.f ? request.rounding : std::min(kMaxNormalSmoothing, longer / 32.0f);
    const f32 depth = request.depth != 0.f ? request.depth : longer / 16.0f;
    const f32 share = request.detail != 0.f ? request.detail : kDefaultDerivedNormalDetail;
    const i32 radius = smoothingRadius(rounding);
    if (pixels > kMaxGenerationData / sizeof(double) ||
        pixels * sizeof(double) + smoothingRingBytes(width, radius) > kMaxGenerationData)
        throw Trap("generation data exceeds 64 MiB", ErrorReason::OutOfMemory, ErrorStage::Execute);

    Canvas result;
    result.width = width; result.height = height;
    result.scaleX = image.scaleX; result.scaleY = image.scaleY;
    initializeWork(result.pixels, static_cast<std::size_t>(pixels), kFlatDirection, work);
    std::vector<double> plane;
    initializeWork(plane, static_cast<std::size_t>(pixels), 0.0, work);
    for (u32 y = 0; y < height; ++y) {
        work.spendEach(width);
        const auto first = static_cast<std::size_t>(y) * width;
        for (u32 x = 0; x < width; ++x) plane[first + x] = static_cast<double>(image.pixels[first + x].a);
    }
    if (radius) smoothHeightPlane(plane, width, height, rounding, work);
    {
        const double detail = static_cast<double>(share);
        for (u32 y = 0; y < height; ++y) {
            work.spendEach(width);
            const auto first = static_cast<std::size_t>(y) * width;
            for (u32 x = 0; x < width; ++x) {
                const auto pixel = image.pixels[first + x];
                const u32 luma = (54u * pixel.r + 183u * pixel.g + 19u * pixel.b + 128u) >> 8u;
                const double lit = (static_cast<double>(pixel.a) * static_cast<double>(luma)) / 255.0;
                plane[first + x] = (1.0 - detail) * plane[first + x] + detail * lit;
            }
        }
    }
    const double k = static_cast<double>(depth) / 255.0;
    encodeDirections(plane, width, height, k, [](std::size_t) { return u8{255}; }, result.pixels, work);
    return result;
}

bool stepNormalMap(Execution& ex, Instruction ins) {
    if (ins.opcode != Opcode::SurfaceNormal) return false;
    if (ins.aux > 1) ex.trap("unknown surface.normal receiver kind", ErrorReason::InvalidInstruction);
    const f32 smoothing = ex.popF32();
    const f32 depth = ex.popF32();
    if (!std::isfinite(smoothing) || smoothing < 0 || smoothing > kMaxNormalSmoothing)
        ex.trap("normal smoothing must be finite and in 0..64", ErrorReason::NumericRange);
    if (!std::isfinite(depth) || depth < 0)
        ex.trap("normal depth must be finite and nonnegative", ErrorReason::NumericRange);
    if (ins.aux == 1) {
        // Smoothing and depth are in the receiver's raster: its scale need
        // not match the active target, as for blur.
        const auto mask = ex.popMask(false);
        ex.push(LayerValue{heightDirections(ex, *mask.buffer, true, depth, smoothing), mask.mapping});
    } else {
        const auto image = ex.popLayer();
        validateSurfaceView(ex, *image.buffer, image.mapping, false);
        if (image.buffer->encoding == SurfaceBuffer::Encoding::Direction)
            ex.trap("normal needs a height layer, not a direction layer", ErrorReason::RuntimeViolation);
        ex.push(LayerValue{heightDirections(ex, *image.buffer, false, depth, smoothing), image.mapping});
    }
    ++ex.frame().ip;
    return true;
}

void publishAuxImages(Execution& ex, const DerivedNormalRequest* derived) {
    // Result packing: no instruction is executing any more.
    ex.activeInstruction = {};
    WorkBudget& work = *ex.workBudget;
    work.location = {};
    const auto& exports = ex.file.resourceExports;
    for (std::size_t i = 0; i < exports.size() && i < ex.resourceSlots.size(); ++i) {
        if (exports[i].owner != kRootParent || !exports[i].normal) continue;
        const auto* layer = std::get_if<LayerValue>(&ex.resourceSlots[i]);
        if (!layer || !layer->buffer) break; // declared and never written
        const Canvas& root = ex.canvas();
        auto buffer = std::make_shared<SurfaceBuffer>();
        buffer->raster.width = root.width; buffer->raster.height = root.height;
        buffer->raster.scaleX = root.scaleX; buffer->raster.scaleY = root.scaleY;
        initializeWork(buffer->raster.pixels, static_cast<std::size_t>(root.width) * root.height, kFlatDirection, work);
        // One source-over paint with no clips onto the flat image: the
        // ordinary paint, including the re-orientation of a D4 view.
        RenderTarget flat{buffer, CompositionMode::SourceOver, {}};
        std::swap(ex.target, flat);
        try { paintSurface(ex, *layer, CompositionMode::SourceOver); }
        catch (...) { std::swap(ex.target, flat); throw; }
        std::swap(ex.target, flat);
        AuxImage image;
        image.kind = AuxImageKind::Normal;
        image.source = AuxImageSource::Authored;
        image.name = copyStringWork(ex.file.pool.strings[exports[i].nameIndex], work);
        image.image = std::move(buffer->raster);
        ex.auxImages.push_back(std::move(image));
        break;
    }
    if (derived && ex.auxImages.empty()) {
        AuxImage image;
        image.kind = AuxImageKind::Normal;
        image.source = AuxImageSource::Derived;
        image.name = kDerivedNormalName;
        image.image = deriveNormalImage(ex.canvas(), *derived, work);
        ex.auxImages.push_back(std::move(image));
    }
}

} // namespace detail
} // namespace iprop
