#pragma once

namespace iprop {

// Library version, mirroring the project version in CMakeLists.txt.
constexpr int versionMajor = 0;
constexpr int versionMinor = 7;
constexpr int versionPatch = 2;

// Product version, independent of file, API and ABI versions.
const char* versionString() noexcept;

} // namespace iprop
