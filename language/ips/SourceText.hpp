#pragma once

#include <cstdint>
#include <optional>
#include <string_view>
#include <string>
#include "ips/Diagnostic.hpp"

namespace iprop::ips {

struct Codepoint { std::uint32_t value; std::size_t bytes; };

// RFC 3629 scalar decoding. The lexer keeps byte offsets for slicing text,
// but locations count these codepoints (including tabs as one codepoint).
std::optional<Codepoint> decodeCodepoint(std::string_view source, std::size_t offset);
std::optional<SourcePosition> sourcePositionAt(std::string_view source, std::size_t byteOffset);
std::optional<SourceRange> sourceRangeAt(std::string_view source, SourceLocation location);
std::string sourceContentId(std::string_view source); // SHA-256 of exact bytes

} // namespace iprop::ips
