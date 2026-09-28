#pragma once

#include "iprop/Types.hpp"

namespace iprop {

// Structured output values (spec §5.4/§9.9).
struct OutputPoint {
    f32 x = 0.f;
    f32 y = 0.f;
};
struct OutputRect {
    f32 x = 0.f;
    f32 y = 0.f;
    f32 w = 0.f;
    f32 h = 0.f;
};
struct OutputCircle {
    f32 x = 0.f;
    f32 y = 0.f;
    f32 r = 0.f;
};

} // namespace iprop
