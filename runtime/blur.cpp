#include "iprop/VmInternals.hpp"
#include "iprop/math/Distribution.hpp"

#include <algorithm>
#include <array>
#include <cmath>

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
    if constexpr (Channels == 4) result->raster.pixels.resize(static_cast<std::size_t>(pixels));
    else result->coverage.resize(static_cast<std::size_t>(pixels));
    std::vector<double> cache(static_cast<std::size_t>(cacheBytes / sizeof(double)));
    std::array<double, 2 * kMaxBlurRadius + 1> weights{};
    const double denominator = 2.0 * static_cast<double>(sigma) * static_cast<double>(sigma);
    double total = 0.0;
    for (i32 j = -radius; j <= radius; ++j) {
        const auto index = static_cast<std::size_t>(j + radius);
        weights[index] = math::pinnedExp(-static_cast<double>(j * j) / denominator);
        total = total + weights[index];
    }
    for (u32 j = 0; j < kernel; ++j) weights[j] = weights[j] / total;
    const auto clamp = [](i64 value, u32 extent) {
        return static_cast<u32>(std::clamp(value, i64{0}, static_cast<i64>(extent) - 1));
    };
    u32 nextRow = 0;
    for (u32 y = 0; y < height; ++y) {
        const u32 lastRow = clamp(static_cast<i64>(y) + radius, height);
        for (; nextRow <= lastRow; ++nextRow) {
            auto* row = cache.data() + static_cast<std::size_t>(nextRow % rows) * width * Channels;
            for (u32 x = 0; x < width; ++x) {
                std::array<double, Channels> sum{};
                for (i32 j = -radius; j <= radius; ++j) {
                    const auto index = static_cast<std::size_t>(nextRow) * width + clamp(static_cast<i64>(x) + j, width);
                    const double weight = weights[static_cast<std::size_t>(j + radius)];
                    if constexpr (Channels == 4) {
                        const auto pixel = raster.pixels[index];
                        const double alpha = pixel.a;
                        sum[0] = sum[0] + (static_cast<double>(pixel.r) * alpha / 255.0) * weight;
                        sum[1] = sum[1] + (static_cast<double>(pixel.g) * alpha / 255.0) * weight;
                        sum[2] = sum[2] + (static_cast<double>(pixel.b) * alpha / 255.0) * weight;
                        sum[3] = sum[3] + alpha * weight;
                    } else {
                        const double coverage = source.format == SurfaceBuffer::Format::A8 ? source.coverage[index] : raster.pixels[index].a;
                        sum[0] = sum[0] + coverage * weight;
                    }
                }
                for (std::size_t c = 0; c < Channels; ++c) row[static_cast<std::size_t>(x) * Channels + c] = sum[c];
            }
        }
        for (u32 x = 0; x < width; ++x) {
            std::array<double, Channels> sum{};
            for (i32 j = -radius; j <= radius; ++j) {
                const auto sy = clamp(static_cast<i64>(y) + j, height);
                const auto index = (static_cast<std::size_t>(sy % rows) * width + x) * Channels;
                const double weight = weights[static_cast<std::size_t>(j + radius)];
                for (std::size_t c = 0; c < Channels; ++c) sum[c] = sum[c] + cache[index + c] * weight;
            }
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
