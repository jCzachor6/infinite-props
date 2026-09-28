#include "iprop/Utf8.hpp"

namespace iprop {

bool isValidUtf8(std::span<const u8> bytes) {
    std::size_t i = 0;
    while (i < bytes.size()) {
        const u8 lead = bytes[i];
        if (lead < 0x80) {
            ++i;
            continue;
        }
        std::size_t len = 0;
        u32 cp = 0;
        if ((lead & 0xE0) == 0xC0) {
            len = 1;
            cp = lead & 0x1Fu;
        } else if ((lead & 0xF0) == 0xE0) {
            len = 2;
            cp = lead & 0x0Fu;
        } else if ((lead & 0xF8) == 0xF0) {
            len = 3;
            cp = lead & 0x07u;
        } else {
            return false;
        }
        if (i + len >= bytes.size()) {
            return false;
        }
        for (std::size_t k = 1; k <= len; ++k) {
            const u8 cont = bytes[i + k];
            if ((cont & 0xC0) != 0x80) {
                return false;
            }
            cp = (cp << 6) | (cont & 0x3Fu);
        }
        if (cp > 0x10FFFFu || (cp >= 0xD800u && cp <= 0xDFFFu)) {
            return false;
        }
        const bool overlong = (len == 1 && cp < 0x80u) || (len == 2 && cp < 0x800u) ||
                              (len == 3 && cp < 0x10000u);
        if (overlong) {
            return false;
        }
        i += len + 1;
    }
    return true;
}

} // namespace iprop
