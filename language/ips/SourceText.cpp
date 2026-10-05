#include "ips/SourceText.hpp"

namespace iprop::ips {

std::optional<Codepoint> decodeCodepoint(std::string_view source, std::size_t offset) {
    if (offset >= source.size()) return std::nullopt;
    const auto lead = static_cast<unsigned char>(source[offset]);
    if (lead < 0x80) return Codepoint{lead, 1};
    const std::size_t bytes = lead >= 0xC2 && lead <= 0xDF ? 2 :
                              lead >= 0xE0 && lead <= 0xEF ? 3 :
                              lead >= 0xF0 && lead <= 0xF4 ? 4 : 0;
    if (!bytes || source.size() - offset < bytes) return std::nullopt;
    std::uint32_t value = lead & (0x7Fu >> bytes);
    for (std::size_t i = 1; i < bytes; ++i) {
        const auto next = static_cast<unsigned char>(source[offset + i]);
        if ((next & 0xC0u) != 0x80u) return std::nullopt;
        value = (value << 6u) | (next & 0x3Fu);
    }
    if ((bytes == 3 && value < 0x800) || (bytes == 4 && value < 0x10000) ||
        (value >= 0xD800 && value <= 0xDFFF) || value > 0x10FFFF) return std::nullopt;
    return Codepoint{value, bytes};
}

} // namespace iprop::ips
