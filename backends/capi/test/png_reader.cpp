// The conformance runner is C; it reads golden PNGs through the tools' reader.
#include "test_support.h"

#include "png.hpp"

#include <cstdlib>
#include <cstring>
#include <exception>

extern "C" unsigned char* read_png_rgba(const char* path, int* width, int* height) {
    try {
        const auto image = iprop::tools::readPng(path);
        auto* pixels = static_cast<unsigned char*>(std::malloc(image.rgba.size()));
        if (pixels == nullptr) return nullptr;
        std::memcpy(pixels, image.rgba.data(), image.rgba.size());
        *width = static_cast<int>(image.width);
        *height = static_cast<int>(image.height);
        return pixels;
    } catch (const std::exception&) {
        return nullptr;
    }
}
