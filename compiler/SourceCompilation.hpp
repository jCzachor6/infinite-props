#pragma once

#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "iprop/Types.hpp"
#include "ips/Ast.hpp"
#include "ips/Diagnostic.hpp"
#include "ips/Imports.hpp"

namespace iprop {

struct SourceCompilationOptions {
    bool optimize = true;
    bool stripDebug = true;
    bool compress = true;
    std::size_t diagnosticLimit = 20; // bounded to [1,20] across all sources/stages
    // Authoring tools may validate a material entry without an executable.
    // Asset entries still run the entire compilation/validation pipeline.
    bool allowMaterialEntry = false;
};

using SourceCompilationStatus = ips::DiagnosticStatus;

using CompilationDiagnostic = ips::Diagnostic;

struct SourceCompilationResult {
    SourceCompilationStatus status = SourceCompilationStatus::InternalError;
    std::string entryPath;
    // Owning snapshots are also retained on failure, so renderers never reload
    // a changed file to produce a diagnostic. Programs contain no source views.
    std::map<std::string, std::string> sources;
    std::map<std::string, ips::Program> programs;
    // The file identifier of every import of each loaded file, in order.
    std::map<std::string, std::vector<std::string>> imports;
    std::vector<CompilationDiagnostic> diagnostics;
    std::vector<u8> bytes; // validated executable; empty for failure or an allowed material entry

    bool succeeded() const noexcept { return status == SourceCompilationStatus::Success; }
};

// Shared source/import pipeline: load and parse once per reachable file, check
// types, plan, lower, assemble, optimize and validate entirely in memory.
// The host maps imports to file identifiers through resolveImport (lexical
// ips::resolveImportPath when empty) and identifiers to sources through
// loadSource; this API performs no filesystem writes. The final
// strip/compression options are checked too.
SourceCompilationResult compileSource(
    std::string_view entryPath,
    const std::function<std::string(std::string_view path)>& loadSource,
    SourceCompilationOptions options = {},
    const ips::ImportResolver& resolveImport = {});

} // namespace iprop
