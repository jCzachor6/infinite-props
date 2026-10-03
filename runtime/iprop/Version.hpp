#pragma once

namespace iprop {

// Library version, mirroring the project version in CMakeLists.txt.
constexpr int versionMajor = 0;
constexpr int versionMinor = 4;
constexpr int versionPatch = 0;

// "0.4.0" — stable for the lifetime of the library.
const char* versionString() noexcept;

} // namespace iprop
