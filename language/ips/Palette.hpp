#pragma once
#include "iprop/Value.hpp"
#include "ips/Types.hpp"
#include <span>

namespace iprop::ips {

struct ResolvedPalette {
    ValueType type = ValueType::Void;
    std::vector<Value> elements;
};

ResolvedPalette resolvePalette(const PaletteDecl& palette);
// Legacy literal palettes keep their original diagnostics and lowering.
// New descriptions, vec2 and references use the asset constant resolver.
bool extendedPalette(const PaletteDecl& palette);
ValueType paletteType(const PaletteDecl& palette, std::span<const ValueType> elements);
// Pure §12.11 interpolation for an already checked colour/f32 palette.
iprop::Value sampleRamp(const ResolvedPalette& palette, iprop::f32 t);
const PaletteDecl& findPalette(const Program& owner, const Identifier& name);

} // namespace iprop::ips
