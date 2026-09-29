#include "ips/SourceError.hpp"

#include <sstream>

namespace iprop::ips {

std::string formatError(std::string_view source, const SourceError& error) {
    std::ostringstream out;
    out << error.location.line << ":" << error.location.column << ": error: "
        << error.message << "\n";

    // Extract the offending line.
    std::size_t start = 0;
    for (std::size_t i = 0; i + 1 < error.location.line; ++i) {
        start = source.find('\n', start);
        if (start == std::string_view::npos) {
            break;
        }
        ++start;
    }
    std::size_t end = source.find('\n', start);
    std::string_view lineText = source.substr(start, end - start);

    // Tolerate CRLF sources: a trailing carriage return is not part of the
    // logical line.
    if (!lineText.empty() && lineText.back() == '\r') {
        lineText.remove_suffix(1);
    }

    out << lineText << "\n";
    if (error.location.column >= 1) {
        out << std::string(error.location.column - 1, ' ') << "^\n";
    }
    return out.str();
}

} // namespace iprop::ips
