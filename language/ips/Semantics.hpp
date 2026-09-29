#pragma once

// Semantic analysis for .ips: name resolution, type checking and part-call
// validation. Runs on parsed ASTs; throws SourceError at the location of the
// first problem. The shared type engine lives in Typing.hpp (used by the
// checker here and by the compiler), and import-DAG resolution in
// Imports.hpp.

#include <map>
#include <string>
#include <vector>

#include "ips/Ast.hpp"
#include "ips/Typing.hpp"

namespace iprop::ips {

// Builds a part surface from an asset's declarations.
PartInfo partInfoOf(const Asset& asset);

// Checks one asset: duplicate declarations, name resolution, type checking,
// integer-only conditions, part-call arity and slot kinds, input default
// types. `parts` maps import aliases to the declared surfaces of the
// imported files. Throws SourceError at the location of the first problem.
void checkAsset(const Program& program,
                const std::map<std::string, PartInfo>& parts);

} // namespace iprop::ips
