#pragma once

#include "ips/Ast.hpp"
#include "ips/BuiltinSpec.hpp"
#include "ips/Palette.hpp"
#include "iprop/Value.hpp"
#include <map>
#include <memory>

namespace iprop::ips {

using AssetConstants = std::map<std::string, Value>;
struct AssetDeclarations {
    AssetConstants constants;
    std::map<const PaletteDecl*, ResolvedPalette> palettes;
    // The immutable list value a bare palette name reads, built once.
    std::map<const PaletteDecl*, ListValue> paletteLists;
};
const Asset::Constant* findAssetConstant(const Program& program, const std::string& name);
// Intrinsic eligibility; argument expressions must independently be constant.
bool constantBuiltin(const BuiltinSpec& spec, const BuiltinOverload& overload);
// Resolves the asset's complete declaration graph, including unused constants.
// Forward references are allowed. No helpers or generation state are executed.
AssetConstants resolveAssetConstants(const Program& program);
AssetDeclarations resolveAssetDeclarations(const Program& program);

// Probes runtime expressions of one asset against its already resolved
// declarations, without folding them or executing any generation effects.
// Build one probe per asset: probing never re-resolves the declaration graph.
class ConstantProbe {
public:
    ConstantProbe(const Program& program, AssetDeclarations declarations);
    ~ConstantProbe();
    ConstantProbe(const ConstantProbe&) = delete;
    ConstantProbe& operator=(const ConstantProbe&) = delete;

    // The value of an expression that reads only literals, asset constants
    // and palettes; `locals` shadow those names. Everything else is unknown,
    // including a pure value whose evaluation fails: the VM traps on it only
    // when the expression is actually reached.
    std::optional<Value> known(const Expr& expression,
                               const std::map<std::string, ValueType>& locals);
    // The message with which a constant builtin call, such as a distribution
    // constructor, rejects arguments that are all known. A failure while
    // evaluating an argument leaves the call unknown instead.
    std::optional<std::string> rejection(const Expr& call,
                                         const std::map<std::string, ValueType>& locals);

private:
    struct State;
    std::unique_ptr<State> state_;
};

} // namespace iprop::ips
