#pragma once

#include <iosfwd>
#include <string_view>
#include <vector>

#include "SourceCompilation.hpp"

namespace iprop::tools {

// Preliminary schema 0 with typed stable codes. Byte ranges/versioning are
// introduced by B.3; null fields are not measured source ranges.
void writeSourceDiagnosticsJson(std::ostream& out, std::string_view productVersion,
    std::string_view status, const std::vector<CompilationDiagnostic>& diagnostics);
void writeSourceDiagnosticText(std::ostream& out, const CompilationDiagnostic& diagnostic,
    std::string_view source);

} // namespace iprop::tools
