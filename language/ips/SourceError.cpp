#include "ips/SourceError.hpp"
#include "ips/SourceText.hpp"

#include <sstream>

namespace iprop::ips {

std::string formatDiagnostic(std::string_view source, const Diagnostic& error) {
    std::ostringstream out;
    if (!error.location) {
        out << diagnosticSeverityName(error.severity) << ": " << error.message << '\n';
        return out.str();
    }
    out << error.location->line << ":" << error.location->column << ": "
        << diagnosticSeverityName(error.severity) << ": "
        << error.message << "\n";

    // Extract the offending line.
    std::size_t start = 0;
    for (std::size_t i = 0; i + 1 < error.location->line; ++i) {
        start = source.find('\n', start);
        if (start == std::string_view::npos) {
            break;
        }
        ++start;
    }
    if (start == std::string_view::npos) return out.str();
    std::size_t end = source.find('\n', start);
    std::string_view lineText = source.substr(start, end - start);

    // Tolerate CRLF sources: a trailing carriage return is not part of the
    // logical line.
    if (!lineText.empty() && lineText.back() == '\r') {
        lineText.remove_suffix(1);
    }

    out << lineText << "\n";
    if (error.location->column >= 1) {
        std::size_t offset = 0;
        for (std::size_t column = 1; column < error.location->column && offset < lineText.size(); ++column) {
            out << (lineText[offset] == '\t' ? '\t' : ' ');
            const auto point = decodeCodepoint(lineText, offset);
            offset += point ? point->bytes : 1;
        }
        out << "^\n";
    }
    return out.str();
}

std::string formatError(std::string_view source, const SourceError& error) {
    return formatDiagnostic(source, {error.message,error.path,error.location,error.code,error.stage});
}

} // namespace iprop::ips
