#pragma once
#include "iprop/Value.hpp"
#include "ips/Types.hpp"

namespace iprop::ips {

struct ResolvedPalette {
    ValueType type = ValueType::Void;
    std::vector<Value> elements;
};

ResolvedPalette resolvePalette(const PaletteDecl& palette);
// Pure §12.11 interpolation for an already checked colour/f32 palette.
iprop::Value sampleRamp(const ResolvedPalette& palette, iprop::f32 t);
const PaletteDecl& findPalette(const Program& owner, const Identifier& name);

} // namespace iprop::ips
