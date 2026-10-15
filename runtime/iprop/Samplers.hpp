#pragma once
#include "iprop/Value.hpp"
#include <array>

namespace iprop {
inline constexpr std::array<TypeDescriptor, 4> kSamplerElementTypes{
    TypeDescriptor::I32, TypeDescriptor::F32, TypeDescriptor::Color, TypeDescriptor::Vec2};
// Shared by source diagnostics and runtime capture. Probability mass is
// deliberately checked by the existing draw kernel, only when sampled.
const char* samplerBoundsError(TypeDescriptor item, const Value& lower,
                               const Value& upper, bool range);
}
