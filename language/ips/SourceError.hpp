#pragma once

// Shared diagnostics for the .ips front end: a located source error and a
// caret-snippet formatter. Used by the lexer, the parser and semantic
// analysis.

#include <cstddef>
#include <exception>
#include <string>
#include <string_view>

namespace iprop::ips {

// 1-based line/column.
struct SourceLocation {
    std::size_t line = 1;
    std::size_t column = 1;
};

// A source error with a location. Derives from std::exception so test
// frameworks and tools report the message.
struct SourceError : std::exception {
    SourceLocation location;
    std::string message;

    SourceError(SourceLocation at, std::string text)
        : location(at), message(std::move(text)) {}

    const char* what() const noexcept override { return message.c_str(); }
};

// Formats "line:col: error: <message>" plus the offending source line and
// a caret pointing at the column.
std::string formatError(std::string_view source, const SourceError& error);

} // namespace iprop::ips
