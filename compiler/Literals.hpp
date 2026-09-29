#pragma once

// Parsing of the literal texts produced by the .ips lexer into binary
// values (i32 bit patterns, f32 bit patterns, RGBA8 colours). Shared by the
// constant folder and the assembler.

#include <cstdint>
#include <cstring>
#include <string>

#include "iprop/Types.hpp"

namespace iprop::compiler {

// Integer literal (decimal or 0x hex) → 32-bit pattern.
inline bool parseIntBits(const std::string& text, u32& bits) {
    try {
        std::size_t used = 0;
        const long long value = std::stoll(text, &used, 0);
        if (used != text.size()) {
            return false;
        }
        bits = static_cast<u32>(static_cast<unsigned long long>(value));
        return true;
    } catch (...) {
        return false;
    }
}

// Float literal → IEEE-754 binary32 bits.
inline bool parseFloatBits(const std::string& text, u32& bits) {
    try {
        std::size_t used = 0;
        const f32 value = std::stof(text, &used);
        if (used != text.size()) {
            return false;
        }
        std::memcpy(&bits, &value, sizeof(bits));
        return true;
    } catch (...) {
        return false;
    }
}

// Colour literal (#RRGGBB or #RRGGBBAA) → RGBA8. Missing alpha defaults to
// 255.
inline bool parseColor(const std::string& text, Color& out) {
    if ((text.size() != 7 && text.size() != 9) || text[0] != '#') {
        return false;
    }
    const int count = text.size() == 7 ? 3 : 4;
    u8 channels[4] = {0, 0, 0, 255};
    const auto hex = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    for (int i = 0; i < count; ++i) {
        const int hi = hex(text[1 + i * 2]);
        const int lo = hex(text[2 + i * 2]);
        if (hi < 0 || lo < 0) {
            return false;
        }
        channels[i] = static_cast<u8>(hi * 16 + lo);
    }
    out = {channels[0], channels[1], channels[2], channels[3]};
    return true;
}

} // namespace iprop::compiler
