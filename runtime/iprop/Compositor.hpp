#pragma once

#include "iprop/Types.hpp"

#include <array>
#include <string_view>

namespace iprop {

// Wire mode 0 inherits the destination. A render target always holds an
// effective mode; an immutable image deliberately has no default blend.
enum class CompositionMode : u8 { Inherited = 0, Legacy = 1, SourceOver = 2 };
struct CompositionSpec {
    CompositionMode mode;
    std::string_view name;
    i32 sourceConstant;
};
inline constexpr std::array kCompositionSpecs{
    CompositionSpec{CompositionMode::Legacy, "legacy", 0},
    CompositionSpec{CompositionMode::SourceOver, "source_over", 1},
};
constexpr const CompositionSpec* compositionSpec(std::string_view name) {
    for (const auto& spec : kCompositionSpecs) if (spec.name == name) return &spec;
    return nullptr;
}
constexpr const CompositionSpec* compositionSpec(CompositionMode mode) {
    for (const auto& spec : kCompositionSpecs) if (spec.mode == mode) return &spec;
    return nullptr;
}
constexpr bool validCompositionMode(u16 mode) {
    if (mode == 0) return true;
    for (const auto& spec : kCompositionSpecs) if (mode == static_cast<u16>(spec.mode)) return true;
    return false;
}
constexpr CompositionMode resolveComposition(CompositionMode requested, CompositionMode active) {
    return requested == CompositionMode::Inherited ? active : requested;
}

// Straight RGBA8 in stored channel space. Each operation rounds and stores
// its result; no linear-light conversion or deferred premultiplied buffer.
Color composePixel(Color destination, Color source, CompositionMode mode);

} // namespace iprop
