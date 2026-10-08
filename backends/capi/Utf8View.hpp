#pragma once

#include "iprop.h"
#include "iprop/Utf8.hpp"

#include <string_view>

namespace iprop::capi {

// No allocation, sentinel read, implicit strlen or partial publication.
// Metadata names may contain U+0000: length-counted lookup is exact.
inline bool readUtf8View(iprop_utf8_view view, std::string_view& out) noexcept {
    if (view.data == nullptr && view.byte_count != 0) return false;
    if (view.byte_count == 0) {
        out = {};
        return true;
    }
    if (!isValidUtf8({reinterpret_cast<const u8*>(view.data), view.byte_count})) return false;
    out = std::string_view(view.data, view.byte_count);
    return true;
}

} // namespace iprop::capi
