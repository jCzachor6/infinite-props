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

// Maps the path written in an import of the file `importer` to the
// identifier of the imported file. Equal identifiers denote one file: it is
// loaded and parsed once, and an identifier on the current chain is a cycle.
using ImportResolver =
    std::function<std::string(std::string_view importer, std::string_view path)>;

// The lexical resolver: the importer's directory joined with `path`, with
// `.`, empty and inner `..` segments removed (absolute paths stay absolute).
// Hosts whose files can be symlinks resolve physically instead, as ipc does.
std::string resolveImportPath(std::string_view importer, std::string_view path);

// Resolves the DAG rooted at entryPath from an owning parsed-source cache and
// returns the file identifiers dependencies first, the entry last. Imports
// resolve against their importing file (resolveImportPath when `resolve` is
// empty). Each reachable file is requested once; the returned Program must
// outlive this call. Throws SourceError on cycles (located at the import that
// closes the cycle, naming the chain). SourceError from parsing retains its
// owning path. Other loader failures are located at the importing statement
// (or at the entry for the initial load).
std::vector<std::string> resolveImportOrder(
    std::string_view entryPath,
    const std::function<const Program&(std::string_view path)>& loadProgram,
    const ImportResolver& resolve = {});

} // namespace iprop::ips
