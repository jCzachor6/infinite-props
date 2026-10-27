#pragma once

#include "ips/Ast.hpp"

#include <memory>

namespace iprop::ips {

// Deep copy of an expression tree (AST nodes own children via unique_ptr).
std::unique_ptr<Expr> cloneExpr(const Expr& expr);

// Moves clones of a material file's definitions and its file-local
// palette/const declarations into the importing program's asset.
void mergeMaterialFile(Program& importer, const Program& source);

// Rewrites material creations, field reads and material parameters into
// ordinary scalar locals/parameters before semantic checking. Throws
// SourceError on unknown materials/fields and missing `base`.
void expandMaterials(Program& program);

} // namespace iprop::ips
