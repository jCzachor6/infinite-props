#pragma once

#include "iprop/Types.hpp"
#include <span>
#include <vector>

namespace iprop {

// v0.4 envelope: one zlib/DEFLATE stream holding an entire original file.
// Plain files are copied. Compressed inputs are checked and expanded exactly.
std::vector<u8> decompressFile(std::span<const u8> data);

// Returns a deterministic compressed envelope only when its complete size is
// smaller. Existing envelopes are validated and returned unchanged. Oversized
// plain files remain plain; historical versions are preserved inside the stream.
std::vector<u8> compressFile(std::span<const u8> data);

} // namespace iprop
