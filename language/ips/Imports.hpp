#pragma once

// Import-DAG resolution for .ips: loads the files reachable from an entry
// point through `import` declarations and returns them in dependency order.
// The compiler driver (ipc) uses this before checking and compiling.

#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace iprop::ips {

struct Program;

// Resolves the same DAG from an owning parsed-source cache. Each reachable
// path is requested once; the returned Program must outlive this call.
// SourceError from parsing retains its owning path. Other loader failures are
// located at the importing statement (or at the entry for the initial load).
std::vector<std::string> resolveImportOrder(
    std::string_view entryPath,
    const std::function<const Program&(std::string_view path)>& loadProgram);

// Resolves the import DAG rooted at entryPath: loads each file through
// `loadSource`, parses it and returns the paths in dependency order
// (dependencies first, the entry last). Throws SourceError on cycles
// (located at the import statement that closes the cycle) and on files that
// cannot be loaded.
std::vector<std::string> resolveImports(
    std::string_view entryPath,
    const std::function<std::string(std::string_view path)>& loadSource);

} // namespace iprop::ips
