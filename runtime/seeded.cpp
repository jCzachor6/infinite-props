#include "iprop/VmInternals.hpp"

#include <algorithm>
#include <cmath>

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
            ex.trap("stream stack underflow");
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
    case Opcode::Hash:
        ex.push(Value(static_cast<i32>(hash32(static_cast<u32>(ex.popI32())))));
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
    case Opcode::Lerp: {
        const f32 t = std::clamp(ex.popF32(), 0.f, 1.f);
        const f32 b = ex.popF32();
        const f32 a = ex.popF32();
        ex.push(Value(a * (1.f - t) + b * t));
        ++ex.frame().ip;
        return true;
    }

    default:
        return false;
    }
}

} // namespace iprop::detail
