#pragma once

// Import-DAG resolution for .ips: visits the files reachable from an entry
// point through `import` declarations and returns them in dependency order.
// The compiler's source compilation uses this before checking and compiling.

#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace iprop::ips {

struct Program;

// Resolves the DAG rooted at entryPath from an owning parsed-source cache and
// returns the paths dependencies first, the entry last. Each reachable path is
// requested once; the returned Program must outlive this call. Throws
// SourceError on cycles (located at the import that closes the cycle).
// SourceError from parsing retains its owning path. Other loader failures are
// located at the importing statement (or at the entry for the initial load).
std::vector<std::string> resolveImportOrder(
    std::string_view entryPath,
    const std::function<const Program&(std::string_view path)>& loadProgram);

} // namespace iprop::ips
