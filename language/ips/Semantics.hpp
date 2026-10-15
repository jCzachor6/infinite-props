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

// Layer and clip blocks each open a runtime surface scope, and the validator
// bounds how deeply one function nests them. Throws at the block that would
// open one scope too many while `open` scopes enclose it.
void checkSurfaceScope(std::size_t open, const SourceLocation& at, DiagnosticStage stage);

// Checks one asset: duplicate declarations, name resolution, type checking,
// integer-only conditions, part-call arity and slot kinds, input default
// types. `parts` maps import aliases to the declared surfaces of the
// imported files. Throws the first diagnostic of checkAssetDiagnostics.
void checkAsset(const Program& program,
                const std::map<std::string, PartInfo>& parts);

// Bounded recovery on a complete parsed AST. Independent statements/defaults
// are checked, while expressions depending on a failed declaration remain
// untyped. The caller must not compile an asset with any diagnostics.
std::vector<SourceError> checkAssetDiagnostics(
    const Program& program, const std::map<std::string, PartInfo>& parts,
    std::size_t limit = 20);

} // namespace iprop::ips
