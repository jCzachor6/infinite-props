#pragma once

namespace iprop {

// Library version, mirroring the project version in CMakeLists.txt.
constexpr int versionMajor = 0;
constexpr int versionMinor = 8;
constexpr int versionPatch = 0;

// Product version, independent of file, API and ABI versions.
const char* versionString() noexcept;

} // namespace iprop
