#include "source_diagnostics.hpp"

#include <ostream>

#include "json.hpp"
#include "ips/SourceError.hpp"

namespace iprop::tools {

void writeSourceDiagnosticsJson(std::ostream& out, std::string_view productVersion,
    std::string_view status, const std::vector<CompilationDiagnostic>& diagnostics) {
    out << "{\"schema_version\":0,\"product_version\":\"" << jsonEscape(productVersion)
        << "\",\"language_version\":null,\"status\":\"" << jsonEscape(status)
        << "\",\"diagnostics\":[";
    bool first = true;
    for (const auto& diagnostic : diagnostics) {
        if (!first) out << ',';
        first = false;
        out << "{\"code\":null,\"severity\":\"error\",\"message\":\""
            << jsonEscape(diagnostic.message) << "\",\"file\":";
        if (diagnostic.path.empty()) out << "null";
        else out << '"' << jsonEscape(diagnostic.path) << '"';
        out << ",\"location\":";
        if (diagnostic.location) {
            out << "{\"line\":" << diagnostic.location->line
                << ",\"column\":" << diagnostic.location->column << '}';
        } else out << "null";
        out << ",\"range\":null}";
    }
    out << "]}\n";
}

void writeSourceDiagnosticText(std::ostream& out, const CompilationDiagnostic& diagnostic,
    std::string_view source) {
    out << "ipc: ";
    if (diagnostic.location) {
        out << diagnostic.path << ':'
            << ips::formatError(source, {*diagnostic.location, diagnostic.message, diagnostic.path});
    } else out << diagnostic.message << '\n';
}

} // namespace iprop::tools
