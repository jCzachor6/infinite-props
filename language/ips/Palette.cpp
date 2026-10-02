#include "ips/Palette.hpp"
#include "ips/Fold.hpp"

namespace iprop::ips {

ResolvedPalette resolvePalette(const PaletteDecl& palette) {
    if (palette.elements.empty())
        throw SourceError{palette.location,"palette '"+palette.name+"' needs at least one element"};
    ResolvedPalette result;
    for (const auto& element : palette.elements) {
        const auto* literal = std::get_if<Literal>(element.get());
        if (!literal)
            throw SourceError{locationOf(*element),"palette elements must be literals (compile-time constants)"};
        if (literal->kind == Literal::Kind::String)
            throw SourceError{literal->location,"palette '"+palette.name+"' cannot hold string elements"};
        Value value;
        if (!foldExpression(*element,value))
            throw SourceError{literal->location,"invalid palette literal"};
        const auto type = std::holds_alternative<Color>(value) ? ValueType::Color :
                          std::holds_alternative<f32>(value) ? ValueType::F32 : ValueType::I32;
        if (result.type != ValueType::Void && type != result.type)
            throw SourceError{literal->location,"palette '"+palette.name+"' must have elements of one type ("+
                typeName(result.type)+" and "+typeName(type)+" found)"};
        result.type = type;
        result.elements.push_back(value);
    }
    return result;
}

const PaletteDecl& findPalette(const Program& owner, const Identifier& name) {
    for (const auto& palette : owner.asset.palettes)
        if (palette.name == name.name) return palette;
    throw SourceError{name.location,"'"+name.name+"' is not a palette"};
}

} // namespace iprop::ips
