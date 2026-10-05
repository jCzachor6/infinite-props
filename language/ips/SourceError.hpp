#pragma once

// Shared diagnostics for the .ips front end: a located source error and a
// caret-snippet formatter. Used by the lexer, the parser and semantic
// analysis.

#include <cstddef>
#include <exception>
#include <string>
#include <string_view>
#include "ips/Diagnostic.hpp"

namespace iprop::ips {

// A source error with a location. Derives from std::exception so test
// frameworks and tools report the message.
struct SourceError : std::exception {
    SourceLocation location;
    std::string message;
    std::string path; // optional owning file, relative to the compilation entry
    DiagnosticCode code;
    DiagnosticStage stage;

    SourceError(SourceLocation at, std::string text, std::string sourcePath = {},
                DiagnosticCode reason = DiagnosticCode::Unspecified,
                DiagnosticStage phase = DiagnosticStage::Unspecified)
        : location(at), message(std::move(text)), path(std::move(sourcePath)), code(reason), stage(phase) {}

    const char* what() const noexcept override { return message.c_str(); }
};

// Formats "line:col: error: <message>" plus the offending source line and
// a caret pointing at the column.
std::string formatError(std::string_view source, const SourceError& error);
std::string formatDiagnostic(std::string_view source, const Diagnostic& diagnostic);

} // namespace iprop::ips
