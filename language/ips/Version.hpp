#pragma once

namespace iprop::ips {
// Source-language version is independent of product and binary versions.
// Develop implements the additive 0.6 source contract; files that use a 0.5
// instruction lower to format 0.5, all others stay 0.4.
inline constexpr unsigned kLanguageVersionMajor=0;
inline constexpr unsigned kLanguageVersionMinor=7;
} // namespace iprop::ips
