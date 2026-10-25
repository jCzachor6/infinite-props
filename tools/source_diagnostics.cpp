#include "source_diagnostics.hpp"

#include <ostream>
#include <sstream>

#include "json.hpp"
#include "ips/SourceError.hpp"
#include "ips/Version.hpp"
#include "ips/ExportSchema.hpp"

namespace iprop::tools {
namespace {
void writePosition(std::ostream& out, const ips::SourcePosition& position) {
    out << "{\"byte_offset\":" << position.byteOffset << ",\"line\":" << position.line
        << ",\"column\":" << position.column << '}';
}
void writeRange(std::ostream& out, const ips::SourceRange& range) {
    out << "{\"start\":"; writePosition(out,range.start);
    out << ",\"end\":"; writePosition(out,range.end); out << '}';
}
void writeString(std::ostream& out, const std::optional<std::string>& value) {
    if (value) out << '\"' << jsonEscape(*value) << '\"';
    else out << "null";
}
} // namespace

void writeSourceDiagnosticsJson(std::ostream& destination, std::string_view productVersion,
    ips::DiagnosticStatus status, const std::vector<CompilationDiagnostic>& diagnostics) {
    // Build one complete document before publishing any bytes. An exception
    // while escaping/allocating can then use the main fallback without
    // appending a second document to a partially written JSON prefix.
    std::ostringstream buffer;
    buffer.exceptions(std::ios::badbit | std::ios::failbit);
    auto& out=buffer;
    out << "{\"schema_version\":" << ips::kSourceDiagnosticsSchemaVersion << ",\"product_version\":\"" << jsonEscape(productVersion)
        << "\",\"language_version\":{\"major\":" << ips::kLanguageVersionMajor
        << ",\"minor\":" << ips::kLanguageVersionMinor << ",\"patch\":" << ips::kLanguageVersionPatch
        << "},\"status\":\"" << ips::diagnosticStatusName(status)
        << "\",\"diagnostics\":[";
    bool first = true;
    for (const auto& diagnostic : diagnostics) {
        if (!first) out << ',';
        first = false;
        out << "{\"code\":\"" << ips::diagnosticCodeName(diagnostic.code)
            << "\",\"stage\":\"" << ips::diagnosticStageName(diagnostic.stage)
            << "\",\"severity\":\"" << ips::diagnosticSeverityName(diagnostic.severity)
            << "\",\"message\":\""
            << jsonEscape(diagnostic.message) << "\",\"file\":";
        if (diagnostic.path.empty()) out << "null";
        else out << '"' << jsonEscape(diagnostic.path) << '"';
        out << ",\"location\":";
        if (diagnostic.location) {
            out << "{\"line\":" << diagnostic.location->line
                << ",\"column\":" << diagnostic.location->column << '}';
        } else out << "null";
        out << ",\"range\":";
        if (diagnostic.range) writeRange(out,*diagnostic.range); else out << "null";
        out << ",\"source_id\":"; writeString(out,diagnostic.sourceId);
        out << ",\"expected_type\":"; writeString(out,diagnostic.expectedType);
        out << ",\"actual_type\":"; writeString(out,diagnostic.actualType);
        out << ",\"suggestions\":[";
        for (std::size_t i=0;i<diagnostic.suggestions.size();++i) {
            if (i) out << ',';
            out << '\"' << jsonEscape(diagnostic.suggestions[i]) << '\"';
        }
        out << "],\"related_locations\":[";
        for (std::size_t i=0;i<diagnostic.relatedLocations.size();++i) {
            if (i) out << ',';
            const auto& related=diagnostic.relatedLocations[i];
            out << "{\"file\":\"" << jsonEscape(related.path) << "\",\"message\":\""
                << jsonEscape(related.message) << "\",\"range\":";
            writeRange(out,related.range); out << '}';
        }
        out << "],\"fix_its\":[";
        for (std::size_t i=0;i<diagnostic.fixIts.size();++i) {
            if (i) out << ',';
            const auto& fix=diagnostic.fixIts[i];
            out << "{\"range\":"; writeRange(out,fix.range);
            out << ",\"replacement\":\"" << jsonEscape(fix.replacement)
                << "\",\"source_id\":\"" << jsonEscape(fix.sourceId) << "\"}";
        }
        out << "]}";
    }
    out << "]}\n";
    destination << buffer.str();
}

void writeSourceDiagnosticText(std::ostream& out, const CompilationDiagnostic& diagnostic,
    std::string_view source) {
    out << "ipc: ";
    if (diagnostic.location) out << diagnostic.path << ':';
    out << ips::formatDiagnostic(source, diagnostic);
}

} // namespace iprop::tools
