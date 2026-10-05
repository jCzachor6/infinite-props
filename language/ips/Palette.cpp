#include "ips/Palette.hpp"
#include "ips/Suggestions.hpp"
#include "ips/DeclarationConstants.hpp"
#include "iprop/math/Color.hpp"
#include "iprop/math/Curves.hpp"
#include "iprop/math/MinMax.hpp"
#include "iprop/math/Scalar.hpp"

namespace iprop::ips {

ResolvedPalette resolvePalette(const PaletteDecl& palette) {
    if (palette.elements.empty())
        throw SourceError{palette.location,"palette '"+palette.name+"' needs at least one element"};
    ResolvedPalette result;
    for (const auto& element : palette.elements) {
        const Value value = resolveDeclarationConstant(*element).value;
        const auto type = std::holds_alternative<Color>(value) ? ValueType::Color :
                          std::holds_alternative<f32>(value) ? ValueType::F32 : ValueType::I32;
        if (result.type != ValueType::Void && type != result.type)
            throw SourceError{locationOf(*element),"palette '"+palette.name+"' must have elements of one type ("+
                typeName(result.type)+" and "+typeName(type)+" found)"};
        result.type = type;
        result.elements.push_back(value);
    }
    return result;
}

const PaletteDecl& findPalette(const Program& owner, const Identifier& name) {
    std::vector<std::string> names;
    for (const auto& palette : owner.asset.palettes)
        if (palette.name == name.name) return palette;
        else names.push_back(palette.name);
    throw SourceError{name.location,"'"+name.name+"' is not a palette" + didYouMean(name.name, names)};
}

Value sampleRamp(const ResolvedPalette& palette, f32 t) {
    if (palette.elements.size() == 1) return palette.elements.front();
    t = math::f32MinMax(math::f32MinMax(t, 0.f, true), 1.f, false);
    const f32 position = t * static_cast<f32>(palette.elements.size() - 1);
    const auto index = static_cast<std::size_t>(math::f32ToI32(position));
    if (index + 1 >= palette.elements.size()) return palette.elements.back();
    const f32 fraction = position - static_cast<f32>(index);
    const auto& a = palette.elements[index];
    const auto& b = palette.elements[index + 1];
    if (palette.type == ValueType::Color)
        return math::colorMix(std::get<Color>(a), std::get<Color>(b), fraction);
    return math::lerp(std::get<f32>(a), std::get<f32>(b), fraction);
}

} // namespace iprop::ips
