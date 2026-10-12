#pragma once

#include "ips/Ast.hpp"
#include "ips/BuiltinSpec.hpp"
#include "iprop/Value.hpp"
#include <map>

namespace iprop::ips {

using AssetConstants = std::map<std::string, Value>;
const Asset::Constant* findAssetConstant(const Program& program, const std::string& name);
// Intrinsic eligibility; argument expressions must independently be constant.
bool constantBuiltin(const BuiltinSpec& spec, const BuiltinOverload& overload);
// Resolves the asset's complete declaration graph, including unused constants.
// Forward references are allowed. No helpers or generation state are executed.
AssetConstants resolveAssetConstants(const Program& program);

} // namespace iprop::ips
