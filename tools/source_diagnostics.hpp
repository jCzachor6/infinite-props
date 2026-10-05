#pragma once

#include <iosfwd>
#include <string_view>
#include <vector>

#include "SourceCompilation.hpp"

namespace iprop::tools {

// Preliminary schema 0. Stable codes and byte ranges are introduced by B.2/B.3;
// null fields must not be interpreted as measured source ranges.
void writeSourceDiagnosticsJson(std::ostream& out, std::string_view productVersion,
    std::string_view status, const std::vector<CompilationDiagnostic>& diagnostics);
void writeSourceDiagnosticText(std::ostream& out, const CompilationDiagnostic& diagnostic,
    std::string_view source);

} // namespace iprop::tools
