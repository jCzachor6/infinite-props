#pragma once

// PNG files for the CLI tools and tests: 8-bit RGBA without interlacing, the
// only form the tools write and the golden images use. Compression uses the
// zlib the runtime already links: pixels round-trip exactly, while the file
// bytes follow the zlib version, as `.iprop` compression already does.

#include "iprop/Canvas.hpp"

#include <span>
#include <string>
#include <vector>

namespace iprop::tools {

struct Image {
    u32 width = 0;
    u32 height = 0;
    std::vector<u8> rgba; // row-major from the top, 4 bytes per pixel
};

Image canvasImage(const Canvas& canvas);

std::vector<u8> encodePng(const Image& image);
// Throws std::runtime_error for any other PNG form or a damaged file.
Image decodePng(std::span<const u8> bytes);

// writePng returns false when the file cannot be written; readPng throws.
bool writePng(const std::string& path, const Image& image);
Image readPng(const std::string& path);

} // namespace iprop::tools
