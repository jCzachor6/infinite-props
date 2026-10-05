#pragma once

#include <iosfwd>
#include <string_view>
#include <vector>

#include "SourceCompilation.hpp"

namespace iprop::tools {

// Schema 1: positions use UTF-8 byte offsets and one-based codepoint columns;
// ranges are end-exclusive and source identity covers exact snapshot bytes.
void writeSourceDiagnosticsJson(std::ostream& out, std::string_view productVersion,
    std::string_view status, const std::vector<CompilationDiagnostic>& diagnostics);
void writeSourceDiagnosticText(std::ostream& out, const CompilationDiagnostic& diagnostic,
    std::string_view source);

} // namespace iprop::tools
