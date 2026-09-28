#pragma once

#include "iprop/Types.hpp"

#include <cstring>
#include <span>
#include <vector>

namespace iprop {

// Bounds-checked little-endian byte views (spec §2: all multi-byte values are
// little-endian). Reads and writes are explicit and defined: out-of-range
// access throws FormatError, never reads past the buffer.

class Reader {
public:
    explicit Reader(std::span<const u8> data) : data_(data) {}

    std::size_t position() const { return pos_; }
    std::size_t remaining() const { return data_.size() - pos_; }
    bool empty() const { return pos_ == data_.size(); }

    void skip(std::size_t n) {
        require(n);
        pos_ += n;
    }

    u8 readU8() {
        require(1);
        return data_[pos_++];
    }

    u16 readU16() {
        require(2);
        u16 v = readU8();
        v |= u16(readU8()) << 8;
        return v;
    }

    u32 readU32() {
        require(4);
        u32 v = readU8();
        v |= u32(readU8()) << 8;
        v |= u32(readU8()) << 16;
        v |= u32(readU8()) << 24;
        return v;
    }

    u64 readU64() {
        require(8);
        u64 v = 0;
        for (unsigned shift = 0; shift < 64; shift += 8) {
            v |= u64(readU8()) << shift;
        }
        return v;
    }

    i32 readI32() { return static_cast<i32>(readU32()); }

    f32 readF32() {
        const u32 bits = readU32();
        f32 value = 0.f;
        std::memcpy(&value, &bits, sizeof(value));
        return value;
    }

    Color readColor() {
        require(4);
        return {readU8(), readU8(), readU8(), readU8()};
    }

    // A view into the buffer; valid until the Reader is destroyed.
    std::span<const u8> readBytes(std::size_t n) {
        require(n);
        const auto bytes = data_.subspan(pos_, n);
        pos_ += n;
        return bytes;
    }

private:
    void require(std::size_t n) const {
        if (pos_ > data_.size() || n > data_.size() - pos_) {
            throw FormatError("unexpected end of input", pos_);
        }
    }

    std::span<const u8> data_;
    std::size_t pos_ = 0;
};

class Writer {
public:
    void writeU8(u8 v) { data_.push_back(v); }

    void writeU16(u16 v) {
        data_.push_back(u8(v));
        data_.push_back(u8(v >> 8));
    }

    void writeU32(u32 v) {
        data_.push_back(u8(v));
        data_.push_back(u8(v >> 8));
        data_.push_back(u8(v >> 16));
        data_.push_back(u8(v >> 24));
    }

    void writeU64(u64 v) {
        for (unsigned shift = 0; shift < 64; shift += 8) {
            data_.push_back(u8(v >> shift));
        }
    }

    void writeI32(i32 v) { writeU32(static_cast<u32>(v)); }

    void writeF32(f32 v) {
        u32 bits = 0;
        std::memcpy(&bits, &v, sizeof(bits));
        writeU32(bits);
    }

    void writeColor(Color c) {
        writeU8(c.r);
        writeU8(c.g);
        writeU8(c.b);
        writeU8(c.a);
    }

    void writeBytes(std::span<const u8> bytes) {
        data_.insert(data_.end(), bytes.begin(), bytes.end());
    }

    // Pad with zero bytes until the stream length is a multiple of
    // `alignment` (spec §2.3: sections are 4-byte aligned).
    void padTo(std::size_t alignment) {
        while (data_.size() % alignment != 0) {
            writeU8(0);
        }
    }

    std::size_t size() const { return data_.size(); }
    const std::vector<u8>& data() const { return data_; }

private:
    std::vector<u8> data_;
};

} // namespace iprop
