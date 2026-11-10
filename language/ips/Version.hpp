#pragma once

namespace iprop::ips {
// Source-language version is independent of product and binary versions.
// Patch releases identify additive source capabilities independently of the
// unversioned file format and the C API/ABI compatibility lines.
inline constexpr unsigned kLanguageVersionMajor=1;
inline constexpr unsigned kLanguageVersionMinor=0;
inline constexpr unsigned kLanguageVersionPatch=0;
} // namespace iprop::ips
