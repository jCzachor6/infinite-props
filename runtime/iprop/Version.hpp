#pragma once

namespace iprop {

// Library version, mirroring the project version in CMakeLists.txt.
constexpr int versionMajor = 0;
constexpr int versionMinor = 3;
constexpr int versionPatch = 0;

// "0.3.0" — stable for the lifetime of the library.
const char* versionString() noexcept;

} // namespace iprop
