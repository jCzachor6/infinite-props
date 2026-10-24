#include "iprop/Compression.hpp"
#include "iprop/Binary.hpp"
#include "iprop/Format.hpp"

#include <zlib.h>

namespace iprop {

std::vector<u8> decompressFile(std::span<const u8> data) {
    const auto header = parseHeader(data);
    if (!header.hasCompression()) return {data.begin(), data.end()};
    if (data.size() > kMaxCompressedFileSize)
        throw FormatError("compressed file exceeds size limit", 0, ErrorReason::Compression, ErrorStage::Load, OffsetSpace::Container);
    Reader reader(data, kHeaderSize, data.size(), OffsetSpace::Container);
    const auto size = reader.readU32();
    if (size > kMaxCompressedFileSize || size < kHeaderSize + header.sectionCount * 12u)
        throw FormatError("uncompressed file size out of range", kHeaderSize, ErrorReason::Compression, ErrorStage::Load, OffsetSpace::Container);
    if (reader.empty()) throw FormatError("compressed stream is missing", reader.position(), ErrorReason::Compression, ErrorStage::Load, OffsetSpace::Container);
    const auto stream = reader.readBytes(reader.remaining());
    std::vector<u8> expanded(size);
    // Both buffers are bounded to 64 MiB, below zlib's uInt limit. Use the
    // baseline inflate API: Android exposes uncompress2 only from API 28.
    // Retain exact decoded-size and consumed-input checks on every platform.
    z_stream decoder{};
    decoder.next_in = const_cast<Bytef*>(stream.data());
    decoder.avail_in = static_cast<uInt>(stream.size());
    decoder.next_out = expanded.data();
    decoder.avail_out = static_cast<uInt>(expanded.size());
    if (inflateInit(&decoder) != Z_OK)
        throw FormatError("cannot initialize decompressor", kHeaderSize + 4, ErrorReason::Compression, ErrorStage::Load, OffsetSpace::Container);
    const int result = inflate(&decoder, Z_FINISH);
    const auto outputSize = decoder.total_out;
    const auto inputSize = decoder.total_in;
    inflateEnd(&decoder);
    if (result != Z_STREAM_END || outputSize != size)
        throw FormatError("invalid compressed stream or decoded size", kHeaderSize + 4, ErrorReason::Compression, ErrorStage::Load, OffsetSpace::Container);
    if (inputSize != stream.size())
        throw FormatError("trailing bytes after compressed stream", kHeaderSize + 4 + inputSize, ErrorReason::Compression, ErrorStage::Load, OffsetSpace::Container);
    const auto inner = parseHeader(expanded, OffsetSpace::ExpandedFile);
    if (inner.hasCompression()) throw FormatError("nested compressed files are forbidden", kHeaderSize + 4, ErrorReason::Compression, ErrorStage::Load, OffsetSpace::Container);
    if (inner.sectionCount != header.sectionCount || inner.hasDebug() != header.hasDebug())
        throw FormatError("compressed header does not match inner file", kHeaderSize + 4, ErrorReason::Compression, ErrorStage::Load, OffsetSpace::Container);
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
    if (result != Z_OK) throw FormatError("cannot compress file", ErrorReason::Compression, ErrorStage::Load);
    if (kHeaderSize + 4 + size >= data.size()) return {data.begin(), data.end()};
    stream.resize(size);
    Writer encoded;
    for (const auto magic : kMagic) encoded.writeU8(magic);
    encoded.writeU32(0); // reserved
    encoded.writeU32(header.flags | kFlagCompressed);
    encoded.writeU32(header.sectionCount);
    encoded.writeU32(0);
    encoded.writeU32(static_cast<u32>(data.size()));
    encoded.writeBytes(stream);
    return encoded.data();
}
} // namespace iprop
