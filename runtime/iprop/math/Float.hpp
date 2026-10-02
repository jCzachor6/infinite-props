#pragma once

#include "iprop/Types.hpp"

#include <bit>

namespace iprop::math {

inline bool isNaN(f32 value) {
    return (std::bit_cast<u32>(value) & 0x7FFFFFFFu) > 0x7F800000u;
}

// New vector arithmetic has one NaN encoding (spec §9.4), independent of
// backend operand selection. Transport and historical scalar ops retain bits.
inline f32 canonical(f32 value) {
    // Classify the representation, not an FP predicate: optimizers may fold
    // isnan(sqrt(...)) while preserving a different NaN sign/payload.
    const u32 bits = std::bit_cast<u32>(value);
    return std::bit_cast<f32>((bits & 0x7FFFFFFFu) > 0x7F800000u
                                 ? u32{0x7FC00000} : bits);
}

inline Vec2 canonical(Vec2 value) {
    return {canonical(value.x), canonical(value.y)};
}

} // namespace iprop::math
