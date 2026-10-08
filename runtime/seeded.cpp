#include "iprop/VmInternals.hpp"
#include "iprop/math/Curves.hpp"
#include "iprop/math/Distribution.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace iprop::detail {
namespace {

// splitmix32 (spec §12.1), pinned bit-exactly.
u32 hash32(u32 x) {
    x ^= x >> 16u;
    x *= 0x7feb352du;
    x ^= x >> 15u;
    x *= 0x846ca68bu;
    x ^= x >> 16u;
    return x;
}

// [0, 1) from the top 24 bits of a hash (spec §12.2).
f32 hashUnit(u32 h) {
    return static_cast<f32>(h >> 8u) / 16777216.0f;
}

// spec §12.6: t clamped, t*t*(3-2t).
f32 smoothStep(f32 t) {
    t = std::clamp(t, 0.f, 1.f);
    return t * t * (3.f - 2.f * t);
}

// Lattice corner value (spec §12.4): wrapping u32 products.
f32 latticeValue(u32 seed, i32 a, i32 b) {
    const u32 h = hash32(seed ^ (static_cast<u32>(a) * 73856093u) ^
                         (static_cast<u32>(b) * 19349663u));
    return hashUnit(h);
}

// Smoothstep-interpolated value noise (spec §12.4).
f32 noise(f32 x, f32 y, u32 seed) {
    const i32 ix = static_cast<i32>(std::floor(x));
    const i32 iy = static_cast<i32>(std::floor(y));
    const f32 u = smoothStep(x - static_cast<f32>(ix));
    const f32 v = smoothStep(y - static_cast<f32>(iy));
    const f32 p = latticeValue(seed, ix, iy) * (1.f - u) + latticeValue(seed, ix + 1, iy) * u;
    const f32 q =
        latticeValue(seed, ix, iy + 1) * (1.f - u) + latticeValue(seed, ix + 1, iy + 1) * u;
    return p * (1.f - v) + q * v;
}

// Triangular-interpolated noise (spec §12.5).
f32 fault(f32 x, f32 y, u32 seed) {
    const i32 ix = static_cast<i32>(std::floor(x));
    const i32 iy = static_cast<i32>(std::floor(y));
    const f32 u = x - static_cast<f32>(ix);
    const f32 v = y - static_cast<f32>(iy);
    if (u + v < 1.f) {
        return latticeValue(seed, ix, iy) * (1.f - u - v) +
               latticeValue(seed, ix + 1, iy) * u + latticeValue(seed, ix, iy + 1) * v;
    }
    return latticeValue(seed, ix + 1, iy + 1) * (u + v - 1.f) +
           latticeValue(seed, ix, iy + 1) * (1.f - u) +
           latticeValue(seed, ix + 1, iy) * (1.f - v);
}

// ---- the noise v2 (spec §12.10) -------------------------------------------

constexpr i32 kExactCellLimit = 16777216; // 2^24: every integer is exact in f32

// Check in double before converting: f32(INT32_MAX) rounds up to 2^31.
// Bounds include the neighbours each algorithm needs, not just the base cell.
i32 latticeIndex(Execution& ex, f32 coordinate, i32 lower, i32 upper) {
    if (!std::isfinite(coordinate)) {
        ex.trap("noise coordinate out of range", ErrorReason::NumericRange);
    }
    const f32 floored = std::floor(coordinate);
    const double index = static_cast<double>(floored);
    if (index < static_cast<double>(lower) || index > static_cast<double>(upper)) {
        ex.trap("noise coordinate out of range", ErrorReason::NumericRange);
    }
    return static_cast<i32>(floored);
}

// Absolute feature coordinates must remain inside [cell, cell+1) after
// rounding. Some large i32 cells contain no f32 at all; reject those cells.
f32 jitterCoordinate(Execution& ex, i32 cell, f32 offset) {
    const f32 lower = static_cast<f32>(cell);
    if (static_cast<double>(lower) != static_cast<double>(cell)) {
        ex.trap("cell index is not representable as f32", ErrorReason::NumericRange);
    }
    const f32 coordinate = lower + offset;
    const double upper = static_cast<double>(cell) + 1.0;
    return static_cast<double>(coordinate) >= upper
        ? std::nextafter(coordinate, -std::numeric_limits<f32>::infinity())
        : coordinate;
}

// The cell hash from §12.4 (the u32 wraparound lattice mix).
u32 cellHash(u32 seed, i32 cx, i32 cy) {
    return hash32(seed ^ (static_cast<u32>(cx) * 73856093u) ^
                  (static_cast<u32>(cy) * 19349663u));
}

// The gradient corner: the 8-entry Perlin set selected by h & 7.
f32 gradCorner(u32 seed, i32 cx, i32 cy, f32 fx, f32 fy, f32 ox, f32 oy) {
    const u32 gi = cellHash(seed, cx, cy) & 7u;
    const f32 gx = gi < 4u ? ((gi & 1u) != 0 ? -1.f : 1.f)
                           : (gi == 4u ? 1.f : gi == 5u ? -1.f : 0.f);
    const f32 gy = gi < 4u ? ((gi & 2u) != 0 ? -1.f : 1.f)
                           : (gi == 6u ? 1.f : gi == 7u ? -1.f : 0.f);
    return gx * (fx - ox) + gy * (fy - oy);
}

f32 gradNoise(Execution& ex, f32 x, f32 y, u32 seed) {
    const i32 ix = latticeIndex(ex, x, std::numeric_limits<i32>::min(),
                               std::numeric_limits<i32>::max() - 1);
    const i32 iy = latticeIndex(ex, y, std::numeric_limits<i32>::min(),
                               std::numeric_limits<i32>::max() - 1);
    const f32 fx = x - static_cast<f32>(ix);
    const f32 fy = y - static_cast<f32>(iy);
    const f32 u = math::quinticPolynomial(fx);
    const f32 v = math::quinticPolynomial(fy);
    const f32 n00 = gradCorner(seed, ix, iy, fx, fy, 0.f, 0.f);
    const f32 n10 = gradCorner(seed, ix + 1, iy, fx, fy, 1.f, 0.f);
    const f32 n01 = gradCorner(seed, ix, iy + 1, fx, fy, 0.f, 1.f);
    const f32 n11 = gradCorner(seed, ix + 1, iy + 1, fx, fy, 1.f, 1.f);
    const f32 p = n00 * (1.f - u) + n10 * u;
    const f32 q = n01 * (1.f - u) + n11 * u;
    return p * (1.f - v) + q * v;
}

f32 fbmNoise(Execution& ex, f32 x, f32 y, i32 octaves, u32 seed) {
    octaves = std::clamp(octaves, 1, 8);
    f32 sum = 0.f;
    f32 amplitude = 1.f;
    f32 frequency = 1.f;
    f32 total = 0.f;
    for (i32 i = 0; i < octaves; ++i) {
        const f32 g = gradNoise(ex,
            x * frequency, y * frequency,
            seed + static_cast<u32>(i) * 0x9e3779b9u);
        sum += amplitude * g;
        total += amplitude;
        amplitude *= 0.5f;
        frequency *= 2.f;
    }
    return sum / total;
}

Vec2 cellJitter(Execution& ex, i32 cx, i32 cy, u32 seed) {
    const u32 h = cellHash(seed, cx, cy);
    const f32 u = hashUnit(h);
    const f32 v = hashUnit(hash32(h ^ 0x9e3779b9u));
    return {jitterCoordinate(ex, cx, u), jitterCoordinate(ex, cy, v)};
}

f32 cellNoise(Execution& ex, f32 x, f32 y, u32 seed) {
    // All cells in the 5x5 window must have exactly representable origins.
    const i32 ix = latticeIndex(ex, x, -kExactCellLimit + 2, kExactCellLimit - 2);
    const i32 iy = latticeIndex(ex, y, -kExactCellLimit + 2, kExactCellLimit - 2);
    f32 best = std::numeric_limits<f32>::infinity();
    for (i32 dy = -2; dy <= 2; ++dy) {
        for (i32 dx = -2; dx <= 2; ++dx) {
            const i32 cx = ix + dx;
            const i32 cy = iy + dy;
            const Vec2 point = cellJitter(ex, cx, cy, seed);
            const f32 rx = point.x - x;
            const f32 ry = point.y - y;
            const f32 d = std::sqrt(rx * rx + ry * ry);
            if (d < best) {
                best = d;
            }
        }
    }
    return best;
}

f32 warpNoise(Execution& ex, f32 x, f32 y, f32 amount, u32 seed) {
    if (!std::isfinite(amount)) {
        ex.trap("warp amount out of range", ErrorReason::NumericRange);
    }
    const f32 q1 = fbmNoise(ex, x, y, 4, seed);
    const f32 q2 = fbmNoise(ex, x + 3.7f, y + 9.1f, 4, seed);
    const f32 wx = x + amount * q1;
    const f32 wy = y + amount * q2;
    return fbmNoise(ex, wx, wy, 4, seed);
}

f32 ridgeNoise(Execution& ex, f32 x, f32 y, i32 octaves, u32 seed) {
    octaves = std::clamp(octaves, 1, 8);
    f32 sum = 0.f;
    f32 amplitude = 0.5f;
    f32 frequency = 1.f;
    f32 total = 0.f;
    for (i32 i = 0; i < octaves; ++i) {
        const f32 g = gradNoise(ex,
            x * frequency, y * frequency,
            seed + static_cast<u32>(i) * 0x9e3779b9u);
        const f32 r0 = 1.f - std::fabs(g);
        const f32 r = r0 * r0;
        sum += r * amplitude;
        total += amplitude;
        amplitude *= 0.5f;
        frequency *= 2.f;
    }
    return sum / total;
}

// Uniform inclusive integer range (spec v0.5 §3.1): full u32 draws, values
// past the largest multiple of the width are rejected. The last of
// kRangeDraws draws is accepted; every draw after the first costs 1 fuel.
i32 uniformInt(Execution& ex, i32 lo, i32 hi) {
    constexpr u64 kDrawSpace = u64{1} << 32;
    const u64 width = static_cast<u64>(static_cast<i64>(hi) - lo) + 1; // 1 .. 2^32
    const u64 accepted = kDrawSpace / width * width;
    for (u32 draw = 1;; ++draw) {
        Frame& frame = ex.frame();
        frame.stream += 0x9e3779b9u;
        const u32 x = hash32(frame.stream);
        if (x < accepted || draw == kRangeDraws) {
            return static_cast<i32>(static_cast<u32>(lo) + static_cast<u32>(x % width));
        }
        ex.spend(1);
    }
}

// Uniform f32 range (spec v0.5 §3.2): one unit() step, the pinned lerp, and
// the bounds restored when rounding leaves them.
f32 uniformFloat(Execution& ex, f32 lo, f32 hi) {
    Frame& frame = ex.frame();
    frame.stream += 0x9e3779b9u;
    const f32 u = hashUnit(hash32(frame.stream));
    const f32 value = lo * (1.f - u) + hi * u;
    return value < lo ? lo : value > hi ? hi : value;
}

// A range draw shaped by a distribution (spec v0.5 §4.4): one stream step,
// the coefficient u = (x + 1/2) / 2^32, and 1 fuel per kernel iteration.
double distributionDraw(Execution& ex, const Distribution& distribution, double lo, double hi) {
    if (distribution.family == DistributionFamily::None) {
        ex.trap("distribution is not initialized", ErrorReason::NumericRange);
    }
    Frame& frame = ex.frame();
    frame.stream += 0x9e3779b9u;
    const double u = (static_cast<double>(hash32(frame.stream)) + 0.5) * 0x1p-32;
    const auto draw = math::truncatedDraw(distribution, lo, hi, u);
    switch (draw.status) {
    case math::DrawStatus::Outside:
        ex.trap("random range lies outside the distribution", ErrorReason::NumericRange);
    case math::DrawStatus::NoMass:
        ex.trap("random range has no probability under the distribution", ErrorReason::NumericRange);
    case math::DrawStatus::NoConvergence:
        ex.trap("distribution draw did not converge", ErrorReason::NumericRange);
    case math::DrawStatus::Ok: break;
    }
    ex.spend(draw.steps);
    return draw.value;
}

// Round half away from zero (spec v0.5 §4.4); |x| < 2^31 keeps x - t exact.
i32 roundToInt(double x) {
    double t = std::trunc(x);
    const double fraction = x - t;
    if (fraction >= 0.5) t += 1.0;
    else if (fraction <= -0.5) t -= 1.0;
    return static_cast<i32>(t);
}

Distribution constructed(Execution& ex, DistributionFamily family, std::size_t count, const char* rule) {
    Distribution distribution;
    distribution.family = family;
    for (std::size_t i = count; i > 0; --i) distribution.parameters[i - 1] = ex.popF32();
    if (!math::validDistribution(distribution)) ex.trap(rule, ErrorReason::NumericRange);
    return distribution;
}

// Advances the current stream by the Weyl constant and pushes unit().
void advanceAndPushUnit(Execution& ex) {
    Frame& frame = ex.frame();
    frame.stream += 0x9e3779b9u; // wraps (spec §12.2)
    ex.push(Value(hashUnit(hash32(frame.stream))));
    ++frame.ip;
}

} // namespace

// Seeded primitives (spec §9.6/§9.7, exact algorithms §12).
bool stepSeeded(Execution& ex, Instruction ins) {
    switch (ins.opcode) {
    case Opcode::StreamPush: {
        const u32 seed = static_cast<u32>(ex.popI32());
        Frame& frame = ex.frame();
        frame.savedStreams.push_back(frame.stream);
        frame.stream = seed;
        ++frame.ip;
        return true;
    }
    case Opcode::StreamPop: {
        Frame& frame = ex.frame();
        if (frame.savedStreams.empty()) {
            ex.trap("stream stack underflow", ErrorReason::StackUnderflow);
        }
        frame.stream = frame.savedStreams.back();
        frame.savedStreams.pop_back();
        ++frame.ip;
        return true;
    }
    case Opcode::RngUnit:
        advanceAndPushUnit(ex);
        return true;
    case Opcode::RngRange: {
        const f32 b = ex.popF32();
        const f32 a = ex.popF32();
        Frame& frame = ex.frame();
        frame.stream += 0x9e3779b9u;
        const f32 u = hashUnit(hash32(frame.stream));
        ex.push(Value(a + (b - a) * u));
        ++frame.ip;
        return true;
    }
    case Opcode::RngIndex: {
        const u32 n = static_cast<u32>(ex.popI32());
        Frame& frame = ex.frame();
        frame.stream += 0x9e3779b9u;
        const f32 u = hashUnit(hash32(frame.stream));
        if (n == 0) {
            ex.push(Value(i32{0}));
        } else {
            ex.push(Value(static_cast<i32>(
                std::min(n - 1, static_cast<u32>(u * static_cast<f32>(n))))));
        }
        ++frame.ip;
        return true;
    }
    case Opcode::RngInt: {
        const i32 hi = ex.popI32();
        const i32 lo = ex.popI32();
        if (lo > hi) {
            ex.trap("random range lower bound exceeds the upper bound", ErrorReason::NumericRange);
        }
        ex.push(Value(uniformInt(ex, lo, hi)));
        ++ex.frame().ip;
        return true;
    }
    case Opcode::RngFloat: {
        const f32 hi = ex.popF32();
        const f32 lo = ex.popF32();
        if (!std::isfinite(lo) || !std::isfinite(hi)) {
            ex.trap("random range bounds must be finite", ErrorReason::NumericRange);
        }
        if (lo > hi) {
            ex.trap("random range lower bound exceeds the upper bound", ErrorReason::NumericRange);
        }
        ex.push(Value(uniformFloat(ex, lo, hi)));
        ++ex.frame().ip;
        return true;
    }
    case Opcode::DistributionGaussian:
        ex.push(Value(constructed(ex, DistributionFamily::Gaussian, 2,
                                  "gaussian needs a finite mean and a finite sigma > 0")));
        ++ex.frame().ip;
        return true;
    case Opcode::DistributionTriangular:
        ex.push(Value(constructed(ex, DistributionFamily::Triangular, 3,
                                  "triangular needs finite min <= peak <= max with min < max")));
        ++ex.frame().ip;
        return true;
    case Opcode::DistributionBeta:
        ex.push(Value(constructed(ex, DistributionFamily::Beta, 4,
                                  "beta needs finite alpha > 0, beta > 0 and min < max")));
        ++ex.frame().ip;
        return true;
    case Opcode::RngIntWith: {
        const Distribution distribution = ex.popDistribution();
        const i32 hi = ex.popI32();
        const i32 lo = ex.popI32();
        if (lo > hi) {
            ex.trap("random range lower bound exceeds the upper bound", ErrorReason::NumericRange);
        }
        const double value = distributionDraw(ex, distribution, lo, hi);
        ex.push(Value(roundToInt(value)));
        ++ex.frame().ip;
        return true;
    }
    case Opcode::RngFloatWith: {
        const Distribution distribution = ex.popDistribution();
        const f32 hi = ex.popF32();
        const f32 lo = ex.popF32();
        if (!std::isfinite(lo) || !std::isfinite(hi)) {
            ex.trap("random range bounds must be finite", ErrorReason::NumericRange);
        }
        if (lo > hi) {
            ex.trap("random range lower bound exceeds the upper bound", ErrorReason::NumericRange);
        }
        const f32 value = static_cast<f32>(distributionDraw(ex, distribution, lo, hi));
        ex.push(Value(value < lo ? lo : value > hi ? hi : value));
        ++ex.frame().ip;
        return true;
    }
    case Opcode::Hash:
        ex.push(Value(static_cast<i32>(hash32(static_cast<u32>(ex.popI32())))));
        ++ex.frame().ip;
        return true;
    case Opcode::StreamState:
        ex.push(Value(static_cast<i32>(ex.frame().stream)));
        ++ex.frame().ip;
        return true;

    case Opcode::Noise: {
        const u32 seed = static_cast<u32>(ex.popI32());
        const f32 y = ex.popF32();
        const f32 x = ex.popF32();
        ex.push(Value(noise(x, y, seed)));
        ++ex.frame().ip;
        return true;
    }
    case Opcode::Fault: {
        const u32 seed = static_cast<u32>(ex.popI32());
        const f32 y = ex.popF32();
        const f32 x = ex.popF32();
        ex.push(Value(fault(x, y, seed)));
        ++ex.frame().ip;
        return true;
    }
    case Opcode::Smooth:
        ex.push(Value(smoothStep(ex.popF32())));
        ++ex.frame().ip;
        return true;
    case Opcode::NoiseGrad: {
        const u32 seed = static_cast<u32>(ex.popI32());
        const f32 y = ex.popF32();
        const f32 x = ex.popF32();
        ex.push(Value(gradNoise(ex, x, y, seed)));
        ++ex.frame().ip;
        return true;
    }
    case Opcode::NoiseFbm: {
        const u32 seed = static_cast<u32>(ex.popI32());
        const i32 octaves = ex.popI32();
        const f32 y = ex.popF32();
        const f32 x = ex.popF32();
        ex.push(Value(fbmNoise(ex, x, y, octaves, seed)));
        ++ex.frame().ip;
        return true;
    }
    case Opcode::NoiseCell: {
        const u32 seed = static_cast<u32>(ex.popI32());
        const f32 y = ex.popF32();
        const f32 x = ex.popF32();
        ex.push(Value(cellNoise(ex, x, y, seed)));
        ++ex.frame().ip;
        return true;
    }
    case Opcode::Warp: {
        const u32 seed = static_cast<u32>(ex.popI32());
        const f32 amount = ex.popF32();
        const f32 y = ex.popF32();
        const f32 x = ex.popF32();
        ex.push(Value(warpNoise(ex, x, y, amount, seed)));
        ++ex.frame().ip;
        return true;
    }
    case Opcode::CellJitter: {
        const u32 seed = static_cast<u32>(ex.popI32());
        const i32 cy = ex.popI32();
        const i32 cx = ex.popI32();
        ex.push(Value(cellJitter(ex, cx, cy, seed)));
        ++ex.frame().ip;
        return true;
    }
    case Opcode::NoiseRidge: {
        const u32 seed = static_cast<u32>(ex.popI32());
        const i32 octaves = ex.popI32();
        const f32 y = ex.popF32();
        const f32 x = ex.popF32();
        ex.push(Value(ridgeNoise(ex, x, y, octaves, seed)));
        ++ex.frame().ip;
        return true;
    }

    default:
        return false;
    }
}

} // namespace iprop::detail
