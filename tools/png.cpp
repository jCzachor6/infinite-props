#include "png.hpp"

#include <zlib.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <string_view>

namespace iprop::tools {
namespace {

constexpr u8 kSignature[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};

void put32(std::vector<u8>& out, u32 value) {
    for (int shift = 24; shift >= 0; shift -= 8) out.push_back(static_cast<u8>(value >> shift));
}

u32 get32(std::span<const u8> bytes, std::size_t at) {
    return u32{bytes[at]} << 24 | u32{bytes[at + 1]} << 16 | u32{bytes[at + 2]} << 8 | bytes[at + 3];
}

void chunk(std::vector<u8>& out, std::string_view type, std::span<const u8> data) {
    put32(out, static_cast<u32>(data.size()));
    const auto start = out.size();
    out.insert(out.end(), type.begin(), type.end());
    out.insert(out.end(), data.begin(), data.end());
    put32(out, static_cast<u32>(crc32(0, out.data() + start, static_cast<uInt>(out.size() - start))));
}

// Size of the filtered scanlines (one filter byte per row), within zlib's uLong.
std::size_t scanlineBytes(u32 width, u32 height) {
    const std::uint64_t size = (std::uint64_t{width} * 4 + 1) * height;
    if (width == 0 || height == 0 || size > std::numeric_limits<uLong>::max())
        throw std::runtime_error("PNG image size is out of range");
    return static_cast<std::size_t>(size);
}

u8 paeth(int a, int b, int c) {
    const int p = a + b - c;
    const int pa = std::abs(p - a), pb = std::abs(p - b), pc = std::abs(p - c);
    return static_cast<u8>(pa <= pb && pa <= pc ? a : pb <= pc ? b : c);
}

} // namespace

Image canvasImage(const Canvas& canvas) {
    Image image{canvas.width, canvas.height, {}};
    image.rgba.reserve(canvas.pixels.size() * 4);
    for (const Color& pixel : canvas.pixels)
        image.rgba.insert(image.rgba.end(), {pixel.r, pixel.g, pixel.b, pixel.a});
    return image;
}

std::vector<u8> encodePng(const Image& image) {
    const std::size_t stride = std::size_t{image.width} * 4;
    std::vector<u8> scanlines;
    scanlines.reserve(scanlineBytes(image.width, image.height));
    if (image.rgba.size() != stride * image.height)
        throw std::invalid_argument("PNG pixels do not match the image size");
    // Filter 0 on every row: flat pixel-art colours compress well unpredicted.
    for (u32 y = 0; y < image.height; ++y) {
        scanlines.push_back(0);
        const auto row = image.rgba.begin() + static_cast<std::ptrdiff_t>(y * stride);
        scanlines.insert(scanlines.end(), row, row + static_cast<std::ptrdiff_t>(stride));
    }
    uLongf size = compressBound(static_cast<uLong>(scanlines.size()));
    std::vector<u8> compressed(size);
    if (compress2(compressed.data(), &size, scanlines.data(), static_cast<uLong>(scanlines.size()),
                  Z_BEST_COMPRESSION) != Z_OK)
        throw std::runtime_error("cannot compress PNG pixels");
    compressed.resize(size);

    std::vector<u8> header;
    put32(header, image.width);
    put32(header, image.height);
    header.insert(header.end(), {8, 6, 0, 0, 0}); // 8-bit RGBA, deflate, standard filters, no interlace
    std::vector<u8> png(std::begin(kSignature), std::end(kSignature));
    chunk(png, "IHDR", header);
    chunk(png, "IDAT", compressed);
    chunk(png, "IEND", {});
    return png;
}

Image decodePng(std::span<const u8> bytes) {
    const auto invalid = [](const char* reason) { return std::runtime_error(std::string("invalid PNG: ") + reason); };
    if (bytes.size() < sizeof(kSignature) || !std::equal(std::begin(kSignature), std::end(kSignature), bytes.begin()))
        throw invalid("missing signature");
    Image image;
    std::vector<u8> compressed;
    bool header = false;
    for (std::size_t at = sizeof(kSignature);;) {
        if (bytes.size() - at < 12) throw invalid("truncated chunk");
        const u32 length = get32(bytes, at);
        if (length > bytes.size() - at - 12) throw invalid("chunk extends past the file");
        const auto data = bytes.subspan(at + 8, length);
        if (crc32(0, bytes.data() + at + 4, 4 + length) != get32(bytes, at + 8 + length))
            throw invalid("chunk checksum mismatch");
        const std::string_view type(reinterpret_cast<const char*>(bytes.data() + at + 4), 4);
        at += 12 + std::size_t{length};
        if (type == "IHDR") {
            if (header || length != 13) throw invalid("bad header");
            image.width = get32(data, 0);
            image.height = get32(data, 4);
            if (data[8] != 8 || data[9] != 6 || data[10] != 0 || data[11] != 0 || data[12] != 0)
                throw invalid("only 8-bit RGBA without interlacing is supported");
            header = true;
        } else if (type == "IDAT") {
            if (!header) throw invalid("pixel data before the header");
            compressed.insert(compressed.end(), data.begin(), data.end());
        } else if (type == "IEND") {
            break;
        } else if ((type[0] & 0x20) == 0) {
            throw invalid("unsupported critical chunk");
        }
    }
    if (!header) throw invalid("missing header");

    std::vector<u8> scanlines(scanlineBytes(image.width, image.height));
    uLongf size = static_cast<uLongf>(scanlines.size());
    if (uncompress(scanlines.data(), &size, compressed.data(), static_cast<uLong>(compressed.size())) != Z_OK ||
        size != scanlines.size())
        throw invalid("damaged pixel data");
    const std::size_t stride = std::size_t{image.width} * 4;
    image.rgba.resize(stride * image.height);
    for (std::size_t y = 0; y < image.height; ++y) {
        const u8* in = scanlines.data() + y * (stride + 1);
        u8* row = image.rgba.data() + y * stride;
        const u8* above = y ? row - stride : nullptr;
        for (std::size_t i = 0; i < stride; ++i) {
            const int a = i >= 4 ? row[i - 4] : 0;
            const int b = above ? above[i] : 0;
            const int c = above && i >= 4 ? above[i - 4] : 0;
            int predictor = 0;
            switch (in[0]) {
            case 0: break;
            case 1: predictor = a; break;
            case 2: predictor = b; break;
            case 3: predictor = (a + b) / 2; break;
            case 4: predictor = paeth(a, b, c); break;
            default: throw invalid("unknown row filter");
            }
            row[i] = static_cast<u8>(in[1 + i] + predictor);
        }
    }
    return image;
}

bool writePng(const std::string& path, const Image& image) {
    const auto png = encodePng(image);
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(png.data()), static_cast<std::streamsize>(png.size()));
    return static_cast<bool>(out.flush());
}

Image readPng(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot open PNG '" + path + "'");
    const std::vector<u8> bytes((std::istreambuf_iterator<char>(in)), {});
    return decodePng(bytes);
}

} // namespace iprop::tools
