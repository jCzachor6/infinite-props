#include "iprop/Compression.hpp"
#include "iprop/Binary.hpp"
#include "iprop/Format.hpp"

#include <zlib.h>

namespace iprop {

std::vector<u8> decompressFile(std::span<const u8> data) {
    const auto header = parseHeader(data);
    if (!header.hasCompression()) return {data.begin(), data.end()};
    if (data.size() > kMaxCompressedFileSize)
        throw FormatError("compressed file exceeds size limit", 0);
    Reader reader(data, kHeaderSize, data.size());
    const auto size = reader.readU32();
    if (size > kMaxCompressedFileSize || size < kHeaderSize + header.sectionCount * 12u)
        throw FormatError("uncompressed file size out of range", kHeaderSize);
    if (reader.empty()) throw FormatError("compressed stream is missing", reader.position());
    const auto stream = reader.readBytes(reader.remaining());
    std::vector<u8> expanded(size);
    uLongf outputSize = size;
    uLong inputSize = static_cast<uLong>(stream.size());
    const int result = uncompress2(expanded.data(), &outputSize, stream.data(), &inputSize);
    if (result != Z_OK || outputSize != size)
        throw FormatError("invalid compressed stream or decoded size", kHeaderSize + 4);
    if (inputSize != stream.size())
        throw FormatError("trailing bytes after compressed stream", kHeaderSize + 4 + inputSize);
    const auto inner = parseHeader(expanded);
    if (inner.hasCompression()) throw FormatError("nested compressed files are forbidden", kHeaderSize + 4);
    if (inner.sectionCount != header.sectionCount || inner.hasDebug() != header.hasDebug())
        throw FormatError("compressed header does not match inner file", kHeaderSize + 4);
    (void)parseLayout(expanded);
    return expanded;
}

std::vector<u8> compressFile(std::span<const u8> data) {
    const auto header = parseHeader(data);
    if (header.hasCompression()) {
        (void)decompressFile(data);
        return {data.begin(), data.end()};
    }
    (void)parseLayout(data);
    if (data.size() > kMaxCompressedFileSize) return {data.begin(), data.end()};
    uLongf size = compressBound(static_cast<uLong>(data.size()));
    std::vector<u8> stream(size);
    const int result = compress2(stream.data(), &size, data.data(), static_cast<uLong>(data.size()), Z_BEST_COMPRESSION);
    if (result != Z_OK) throw FormatError("cannot compress file", 0);
    if (kHeaderSize + 4 + size >= data.size()) return {data.begin(), data.end()};
    stream.resize(size);
    Writer encoded;
    for (const auto magic : kMagic) encoded.writeU8(magic);
    encoded.writeU16(kVersionMajor);
    encoded.writeU16(4);
    encoded.writeU32(header.flags | kFlagCompressed);
    encoded.writeU32(header.sectionCount);
    encoded.writeU32(0);
    encoded.writeU32(static_cast<u32>(data.size()));
    encoded.writeBytes(stream);
    return encoded.data();
}
} // namespace iprop
