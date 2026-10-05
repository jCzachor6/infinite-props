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

namespace iprop {

struct SourceCompilationOptions {
    bool optimize = true;
    bool stripDebug = true;
    bool compress = true;
};

enum class SourceCompilationStatus { Success, InvalidSource, InternalError };

using CompilationDiagnostic = ips::Diagnostic;

struct SourceCompilationResult {
    SourceCompilationStatus status = SourceCompilationStatus::InternalError;
    std::string entryPath;
    // Owning snapshots are also retained on failure, so renderers never reload
    // a changed file to produce a diagnostic. Programs contain no source views.
    std::map<std::string, std::string> sources;
    std::map<std::string, ips::Program> programs;
    std::vector<CompilationDiagnostic> diagnostics;
    std::vector<u8> bytes; // empty on failure; validated final artifact on success

    bool succeeded() const noexcept { return status == SourceCompilationStatus::Success; }
};

// Shared source/import pipeline: load and parse once per reachable path, check
// types, plan, lower, assemble, optimize and validate entirely in memory.
// The host defines path resolution through loadSource; this API performs no
// filesystem writes. The final strip/compression options are checked too.
SourceCompilationResult compileSource(
    std::string_view entryPath,
    const std::function<std::string(std::string_view path)>& loadSource,
    SourceCompilationOptions options = {});

} // namespace iprop
