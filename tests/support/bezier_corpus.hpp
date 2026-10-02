#pragma once
#include "iprop/Types.hpp"
#include <array>
#include <bit>
#include <limits>

namespace test_support {

// Deterministic finite, extreme, degenerate and nonfinite query corpus. Hashes
// were recorded from the unprepared kernel at 9bda0c2, before its replacement.
template<class Distance>
iprop::u64 bezierCorpusHash(bool cubic, Distance distance) {
    using namespace iprop;
    u32 state = 0x9e3779b9;
    const auto number = [&]() {
        state ^= state << 13; state ^= state >> 17; state ^= state << 5;
        return static_cast<f32>(static_cast<i32>(state % 8193u)-4096)/16.f;
    };
    u64 hash = 14695981039346656037ull;
    for (int shape=0;shape<104;++shape) {
        std::array<Vec2,4> c;
        for (auto& p:c) p = {number(),number()};
        if (shape == 96) c.fill({3,-2});
        if (shape == 97) c = {{{0,0},{2,0},{4,0},{6,0}}};
        if (shape == 98) c[1] = c[0];
        if (shape == 99) c[3] = c[2];
        if (shape == 100) c[2].x = std::numeric_limits<f32>::infinity();
        if (shape == 101) c[0].y = std::bit_cast<f32>(u32{0xffc01234});
        if (shape == 102) c.fill({1e20f,-1e20f});
        if (shape == 103) c = {{{1e30f,0},{-1e30f,1},{1e30f,-1},{0,1e30f}}};
        for (int query=0;query<33;++query) {
            Vec2 p{number(),number()};
            if (query == 30) p = c[0];
            if (query == 31) p.x = std::numeric_limits<f32>::infinity();
            if (query == 32) p.y = std::bit_cast<f32>(u32{0x7fc01234});
            const u32 bits = std::bit_cast<u32>(distance(cubic,c,p));
            for (u32 shift=0;shift<32;shift+=8) hash = (hash ^ ((bits >> shift)&255u))*1099511628211ull;
        }
    }
    return hash;
}

} // namespace test_support
