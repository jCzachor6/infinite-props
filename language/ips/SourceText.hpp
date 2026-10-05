#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

namespace iprop::ips {

struct Codepoint { std::uint32_t value; std::size_t bytes; };

// RFC 3629 scalar decoding. The lexer keeps byte offsets for slicing text,
// but locations count these codepoints (including tabs as one codepoint).
std::optional<Codepoint> decodeCodepoint(std::string_view source, std::size_t offset);

} // namespace iprop::ips
