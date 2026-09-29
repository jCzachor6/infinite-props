#pragma once

// Semantic analysis for .ips: name resolution, type checking, part-call
// validation and import-DAG resolution. Runs on parsed ASTs; throws
// SourceError at the location of the first problem.

#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "ips/Ast.hpp"

namespace iprop::ips {

// The declared surface of an imported part: its uniforms (with types) and
// structured output slots, as lightweight value types.
struct PartInput {
    InputType type = InputType::Color;
    std::string name;
};

struct PartOutput {
    OutputKind kind = OutputKind::Point;
    bool isList = false;
    std::string name;
};

struct PartInfo {
    std::vector<PartInput> inputs;
    std::vector<PartOutput> outputs;
};

// Builds a part surface from an asset's declarations.
PartInfo partInfoOf(const Asset& asset);

// Checks one asset: duplicate declarations, name resolution, type checking,
// integer-only conditions, part-call arity and slot kinds, input default
// types. `parts` maps import aliases to the declared surfaces of the
// imported files. Throws SourceError at the location of the first problem.
void checkAsset(const Program& program,
                const std::map<std::string, PartInfo>& parts);

// Resolves the import DAG rooted at entryPath: loads each file through
// `loadSource`, parses it and returns the paths in dependency order
// (dependencies first, the entry last). Throws SourceError on cycles
// (located at the import statement that closes the cycle) and on files that
// cannot be loaded.
std::vector<std::string> resolveImports(
    std::string_view entryPath,
    const std::function<std::string(std::string_view path)>& loadSource);

} // namespace iprop::ips
