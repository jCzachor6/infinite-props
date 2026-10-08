#include "ips/SourceText.hpp"

namespace iprop::ips {

std::optional<SourcePosition> sourcePositionAt(std::string_view source, std::size_t offset) {
    if (offset > source.size()) return std::nullopt;
    SourcePosition position;
    while (position.byteOffset < offset) {
        const auto point = decodeCodepoint(source, position.byteOffset);
        const auto length = point ? point->bytes : 1;
        if (length > offset - position.byteOffset) return std::nullopt;
        if (source[position.byteOffset] == '\n') { ++position.line; position.column=1; }
        else ++position.column;
        position.byteOffset += length;
    }
    return position;
}

std::optional<SourceRange> sourceRangeAt(std::string_view source, SourceLocation location) {
    auto offset=location.byteOffset;
    if (offset==std::string_view::npos) {
        SourcePosition position;
        while (position.line!=location.line || position.column!=location.column) {
            if (position.byteOffset>=source.size() || position.line>location.line) return std::nullopt;
            const auto point=decodeCodepoint(source,position.byteOffset);
            if (source[position.byteOffset]=='\n') { ++position.line; position.column=1; }
            else ++position.column;
            position.byteOffset+=point ? point->bytes : 1;
        }
        offset=position.byteOffset;
    }
    const auto start=sourcePositionAt(source,offset);
    if (!start) return std::nullopt;
    auto endOffset=location.endByteOffset;
    if (endOffset==std::string_view::npos) {
        const auto point=decodeCodepoint(source,offset);
        endOffset=offset + (offset==source.size() ? 0 : point ? point->bytes : 1);
    }
    if (endOffset<offset) return std::nullopt;
    const auto end=sourcePositionAt(source,endOffset);
    if (!end) return std::nullopt;
    return SourceRange{*start,*end};
}

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
