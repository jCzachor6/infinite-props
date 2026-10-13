#pragma once

#include "ips/Ast.hpp"
#include "ips/BuiltinSpec.hpp"
#include "ips/Palette.hpp"
#include "iprop/Value.hpp"
#include <map>

namespace iprop::ips {

using AssetConstants = std::map<std::string, Value>;
struct AssetDeclarations {
    AssetConstants constants;
    std::map<const PaletteDecl*, ResolvedPalette> palettes;
};
const Asset::Constant* findAssetConstant(const Program& program, const std::string& name);
// Intrinsic eligibility; argument expressions must independently be constant.
bool constantBuiltin(const BuiltinSpec& spec, const BuiltinOverload& overload);
// Resolves the asset's complete declaration graph, including unused constants.
// Forward references are allowed. No helpers or generation state are executed.
AssetConstants resolveAssetConstants(const Program& program);
AssetDeclarations resolveAssetDeclarations(const Program& program);
// Probe a runtime expression without folding it or executing any generation
// effects. Unknown locals/helper calls return nullopt; invalid pure values
// retain the same located diagnostics as asset constants.
std::optional<Value> knownAssetExpression(const Program& program, const Expr& expression,
                                         const AssetConstants& constants);

} // namespace iprop::ips
