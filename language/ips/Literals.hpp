#pragma once

// Parsing of the literal texts produced by the .ips lexer into binary
// values (i32 bit patterns, f32 bit patterns, RGBA8 colours). Shared by the
// constant folder and the assembler.

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>

#include "iprop/Types.hpp"

namespace iprop::ips {

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

// Float literal → IEEE-754 binary32 bits. Overflow and results that underflow
// to a subnormal or zero are rejected. C libraries disagree on when underflow
// sets ERANGE (glibc, Apple and UCRT all differ), so only the class of the
// rounded result decides.
inline bool parseFloatBits(const std::string& text, u32& bits) {
    char* end = nullptr;
    const f32 value = std::strtof(text.c_str(), &end);
    if (end == text.c_str() || end != text.c_str() + text.size()) {
        return false;
    }
    const int kind = std::fpclassify(value);
    if (kind == FP_SUBNORMAL) return false;
    // Overflow; a spelled-out infinity (or NaN) stays valid.
    if (kind == FP_INFINITE && text.find_first_of("iI") == std::string::npos) return false;
    if (kind == FP_ZERO) {
        // Underflow to zero: a nonzero digit before the exponent.
        const std::size_t x = text.find_first_of("xX");
        const std::size_t begin = x == std::string::npos ? 0 : x + 1;
        const std::size_t exponent = text.find_first_of(x == std::string::npos ? "eE" : "pP", begin);
        const std::string_view mantissa = std::string_view(text).substr(begin, exponent - begin);
        if (mantissa.find_first_of(x == std::string::npos ? "123456789" : "123456789abcdefABCDEF") !=
            std::string_view::npos)
            return false;
    }
    std::memcpy(&bits, &value, sizeof(bits));
    return true;
}

// Colour literal (#RRGGBB or #RRGGBBAA) → RGBA8. Missing alpha defaults to
// 255.
inline bool parseColor(const std::string& text, Color& out) {
    if ((text.size() != 4 && text.size() != 5 && text.size() != 7 && text.size() != 9) || text[0] != '#') {
        return false;
    }
    const bool shorthand = text.size() < 6;
    const int count = text.size() == 4 || text.size() == 7 ? 3 : 4;
    u8 channels[4] = {0, 0, 0, 255};
    const auto hex = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    for (int i = 0; i < count; ++i) {
        const int hi = hex(text[1 + i * (shorthand ? 1 : 2)]);
        const int lo = shorthand ? hi : hex(text[2 + i * 2]);
        if (hi < 0 || lo < 0) {
            return false;
        }
        channels[i] = static_cast<u8>(hi * 16 + lo);
    }
    out = {channels[0], channels[1], channels[2], channels[3]};
    return true;
}

} // namespace iprop::ips
