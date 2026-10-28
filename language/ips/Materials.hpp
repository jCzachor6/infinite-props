#pragma once

#include "ips/Ast.hpp"
#include "ips/SourceError.hpp"

#include <memory>
#include <vector>

namespace iprop::ips {

// Deep copy of an expression tree (AST nodes own children via unique_ptr).
std::unique_ptr<Expr> cloneExpr(const Expr& expr);

// Checks a material file on its own: duplicate materials and fields, the
// required `base`, its palettes and consts, and the type of every field.
// Records each field's type on the file. Errors are located in this file.
std::vector<SourceError> checkMaterialFile(Program& file, std::size_t limit = 20);

// Moves clones of a checked material file's definitions and its file-local
// palette/const declarations into the importing program's asset. The
// palettes and consts are renamed `<alias>__<name>`, so they never collide
// with the importer's own declarations or another material file's. Throws
// at `import` when a material of the same name was merged already.
void mergeMaterialFile(Program& importer, const Program& source, const Import& import);

// Rewrites material creations, field reads and material parameters into
// ordinary scalar locals/parameters before semantic checking. Throws
// SourceError on unknown materials and fields. The merged definitions must
// come from checked material files.
void expandMaterials(Program& program);

} // namespace iprop::ips
