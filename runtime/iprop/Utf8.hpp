#pragma once

#include "iprop/Types.hpp"

#include <span>

namespace iprop {

// RFC 3629 UTF-8 well-formedness: no overlong encodings, no surrogate code
// points, nothing above U+10FFFF, correct continuation bytes. Used to
// validate strings in the const section (spec §5/§14).
bool isValidUtf8(std::span<const u8> bytes);

} // namespace iprop
