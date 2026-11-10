#include "iprop/VmInternals.hpp"
#include "iprop/math/Distribution.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>

namespace iprop::detail {
namespace {

constexpr i32 kMaxBlurRadius = 192;

struct ScratchCharge {
    ResourceArena& arena;
    u64 bytes;
    ScratchCharge(Execution& ex, u64 size) : arena(ex.resources), bytes(size) { ex.allocateResource(size); }
    ~ScratchCharge() { arena.release(bytes); }
};

u8 roundedByte(double value) {
    return static_cast<u8>(std::floor(std::clamp(value, 0.0, 255.0) + 0.5));
}

// The ring retains only horizontal rows needed by the vertical pass. Pixels
// are visited and weights accumulated in increasing offset order, in binary64.
template<std::size_t Channels>
std::shared_ptr<const SurfaceBuffer> filter(Execution& ex, const SurfaceBuffer& source, f32 sigma) {
    const auto& raster = source.raster;
    const auto width = raster.width, height = raster.height;
    const auto radius = static_cast<i32>(std::ceil(3.0 * static_cast<double>(sigma)));
    const auto kernel = static_cast<u32>(2 * radius + 1);
    const auto rows = std::min(height, kernel);
    const u64 pixels = static_cast<u64>(width) * height;
    const u64 resultBytes = Channels == 4 ? surfaceBytes(width, height) : maskBytes(width, height);
    const u64 cacheBytes = static_cast<u64>(width) * rows * Channels * sizeof(double);
    ex.spend(kernel + pixels * (1u + 2u * kernel));
    if (!ex.resources.canAllocate(resultBytes + cacheBytes))
        ex.trap("generation data exceeds 64 MiB", ErrorReason::OutOfMemory);
    const ScratchCharge scratch(ex, cacheBytes);
    ex.allocateResource(resultBytes);
    auto result = ex.resources.makeSurface(resultBytes);
    result->format = Channels == 4 ? SurfaceBuffer::Format::RGBA8 : SurfaceBuffer::Format::A8;
    result->raster.width = width; result->raster.height = height;
    result->raster.scaleX = raster.scaleX; result->raster.scaleY = raster.scaleY;
    if constexpr (Channels == 4) initializeWork(result->raster.pixels, static_cast<std::size_t>(pixels), Color{}, *ex.workBudget);
    else initializeWork(result->coverage, static_cast<std::size_t>(pixels), u8{0}, *ex.workBudget);
    std::vector<double> cache;
    initializeWork(cache, static_cast<std::size_t>(cacheBytes / sizeof(double)), 0.0, *ex.workBudget);
    std::array<double, 2 * kMaxBlurRadius + 1> weights{};
    const double denominator = 2.0 * static_cast<double>(sigma) * static_cast<double>(sigma);
    double total = 0.0;
    for (i32 j = -radius; j <= radius; ++j) {
        ex.work();
        const auto index = static_cast<std::size_t>(j + radius);
        weights[index] = math::pinnedExp(-static_cast<double>(j * j) / denominator);
        total = total + weights[index];
    }
    for (u32 j = 0; j < kernel; ++j) { ex.work(); weights[j] = weights[j] / total; }
    const auto clamp = [](i64 value, u32 extent) {
        return static_cast<u32>(std::clamp(value, i64{0}, static_cast<i64>(extent) - 1));
    };
    // Each sum still adds its taps in increasing offset order, but a whole row
    // advances one tap at a time, so the inner loops are straight runs over
    // adjacent doubles. One uncharged row of doubles holds the premultiplied
    // source row of a horizontal pass, then the sums of a result row. The
    // work steps are those of the per-pixel loops: a pixel visit, its taps and
    // the channel steps, reserved a row at a time before the row is computed.
    const std::size_t rowValues = static_cast<std::size_t>(width) * Channels;
    const std::unique_ptr<double[]> line(new double[rowValues]);
    const u64 columns = width;
    u32 nextRow = 0;
    for (u32 y = 0; y < height; ++y) {
        ex.work();
        const u32 lastRow = clamp(static_cast<i64>(y) + radius, height);
        for (; nextRow <= lastRow; ++nextRow) {
            ex.work();
            double* const row = cache.data() + static_cast<std::size_t>(nextRow % rows) * rowValues;
            ex.workEach(columns * (1u + Channels)); // pixel visits and channel stores
            const auto first = static_cast<std::size_t>(nextRow) * width;
            for (u32 x = 0; x < width; ++x) {
                double* const value = line.get() + static_cast<std::size_t>(x) * Channels;
                if constexpr (Channels == 4) {
                    const auto pixel = raster.pixels[first + x];
                    const double alpha = pixel.a;
                    value[0] = static_cast<double>(pixel.r) * alpha / 255.0;
                    value[1] = static_cast<double>(pixel.g) * alpha / 255.0;
                    value[2] = static_cast<double>(pixel.b) * alpha / 255.0;
                    value[3] = alpha;
                } else {
                    value[0] = source.format == SurfaceBuffer::Format::A8 ? source.coverage[first + x] : raster.pixels[first + x].a;
                }
            }
            std::fill(row, row + rowValues, 0.0);
            for (i32 j = -radius; j <= radius; ++j) {
                ex.workEach(columns); // this tap of every pixel
                const double weight = weights[static_cast<std::size_t>(j + radius)];
                // Columns whose tap falls off either end read the edge pixel.
                const i64 interiorBegin = std::clamp<i64>(-static_cast<i64>(j), 0, width);
                const i64 interiorEnd = std::clamp<i64>(static_cast<i64>(width) - j, interiorBegin, width);
                const double* const leftEdge = line.get();
                const double* const rightEdge = line.get() + rowValues - Channels;
                for (i64 x = 0; x < interiorBegin; ++x)
                    for (std::size_t c = 0; c < Channels; ++c) {
                        double& sum = row[static_cast<std::size_t>(x) * Channels + c];
                        sum = sum + leftEdge[c] * weight;
                    }
                // A row narrower than this tap's offset has no interior: every
                // column reads an edge, and no pointer into the row is formed.
                if (interiorBegin < interiorEnd) {
                    const double* const taps = line.get() + static_cast<std::size_t>(interiorBegin + j) * Channels;
                    double* const sums = row + static_cast<std::size_t>(interiorBegin) * Channels;
                    const auto count = static_cast<std::size_t>(interiorEnd - interiorBegin) * Channels;
                    for (std::size_t i = 0; i < count; ++i) sums[i] = sums[i] + taps[i] * weight;
                }
                for (i64 x = interiorEnd; x < static_cast<i64>(width); ++x)
                    for (std::size_t c = 0; c < Channels; ++c) {
                        double& sum = row[static_cast<std::size_t>(x) * Channels + c];
                        sum = sum + rightEdge[c] * weight;
                    }
            }
        }
        ex.workEach(columns); // pixel visits
        double* const sums = line.get();
        std::fill(sums, sums + rowValues, 0.0);
        for (i32 j = -radius; j <= radius; ++j) {
            ex.workEach(columns * (1u + Channels)); // this tap of every pixel and its channels
            const auto sy = clamp(static_cast<i64>(y) + j, height);
            const double* const from = cache.data() + static_cast<std::size_t>(sy % rows) * rowValues;
            const double weight = weights[static_cast<std::size_t>(j + radius)];
            for (std::size_t i = 0; i < rowValues; ++i) sums[i] = sums[i] + from[i] * weight;
        }
        for (u32 x = 0; x < width; ++x) {
            const double* const sum = sums + static_cast<std::size_t>(x) * Channels;
            const auto index = static_cast<std::size_t>(y) * width + x;
            if constexpr (Channels == 4) {
                const auto alpha = roundedByte(sum[3]);
                result->raster.pixels[index] = alpha == 0 ? Color{} : Color{
                    roundedByte(sum[0] * 255.0 / sum[3]), roundedByte(sum[1] * 255.0 / sum[3]),
                    roundedByte(sum[2] * 255.0 / sum[3]), alpha};
            } else result->coverage[index] = roundedByte(sum[0]);
        }
    }
    return result;
}

} // namespace

bool stepBlur(Execution& ex, Instruction ins) {
    if (ins.opcode != Opcode::LayerBlur && ins.opcode != Opcode::MaskBlur) return false;
    const auto sigma = ex.popF32();
    if (!std::isfinite(sigma) || sigma < 0 || sigma > kMaxBlurSigma)
        ex.trap("blur sigma must be finite and in 0..64", ErrorReason::NumericRange);
    if (ins.opcode == Opcode::LayerBlur) {
        // Sigma is in input raster pixels: the active target's scale is irrelevant.
        auto image = ex.popLayer();
        validateSurfaceView(ex, *image.buffer, image.mapping, false);
        if (sigma != 0) image.buffer = filter<4>(ex, *image.buffer, sigma);
        ex.push(std::move(image));
    } else {
        auto mask = ex.popMask(false);
        if (sigma != 0) mask.buffer = filter<1>(ex, *mask.buffer, sigma);
        ex.push(std::move(mask));
    }
    ++ex.frame().ip;
    return true;
}

} // namespace iprop::detail
