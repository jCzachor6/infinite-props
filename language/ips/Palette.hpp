#pragma once
#include "iprop/Value.hpp"
#include "ips/Types.hpp"

namespace iprop::ips {

struct ResolvedPalette {
    ValueType type = ValueType::Void;
    std::vector<Value> elements;
};

ResolvedPalette resolvePalette(const PaletteDecl& palette);
const PaletteDecl& findPalette(const Program& owner, const Identifier& name);

} // namespace iprop::ips
