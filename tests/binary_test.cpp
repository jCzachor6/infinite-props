#include <catch2/catch_test_macros.hpp>

#include <array>
#include <bit>
#include <cmath>
#include <initializer_list>
#include <limits>

#include "iprop/Binary.hpp"
#include "iprop/Constants.hpp"
#include "iprop/Types.hpp"

using namespace iprop;

namespace {

// Read the full stream and verify a trailing read throws at the expected
// position.
template <typename Fn>
void expectTruncation(const std::vector<u8>& bytes, std::size_t at, Fn&& fn) {
    Reader reader(bytes);
    bool threw = false;
    try {
        fn(reader);
    } catch (const FormatError& error) {
        threw = true;
        REQUIRE(error.offset == at);
    }
    REQUIRE(threw);
}

template <typename T>
void roundTrip(std::initializer_list<T> values, void (Writer::*write)(T), T (Reader::*read)()) {
    Writer w;
    for (const T v : values) (w.*write)(v);
    Reader r(w.data());
    for (const T v : values) REQUIRE((r.*read)() == v);
    REQUIRE(r.empty());
}

} // namespace

TEST_CASE("fixed-width integers and colors round-trip, including high bits and extremes") {
    roundTrip<u8>({0, 0xFF, 0x80}, &Writer::writeU8, &Reader::readU8);
    roundTrip<u16>({0, 0xFFFF, 0x8001, 0x0100}, &Writer::writeU16, &Reader::readU16);
    roundTrip<u32>({0, 0xFFFFFFFFu, 0x12345678u, 0x00000100u}, &Writer::writeU32, &Reader::readU32);
    roundTrip<u64>({0, 0xFFFFFFFFFFFFFFFFull, 0x0102030405060708ull}, &Writer::writeU64, &Reader::readU64);
    roundTrip<i32>({0, -1, std::numeric_limits<i32>::min(), std::numeric_limits<i32>::max()}, &Writer::writeI32, &Reader::readI32);
    roundTrip<Color>({{0, 0, 0, 0}, {255, 255, 255, 255}, {1, 2, 3, 4}}, &Writer::writeColor, &Reader::readColor);
}

TEST_CASE("f32 round-trips preserve bit patterns exactly") {
    const std::array<u32, 6> bits = {0x00000000u, 0x80000000u, std::bit_cast<u32>(1.5f), std::bit_cast<u32>(-2.25f),
        std::bit_cast<u32>(std::numeric_limits<f32>::min()), std::bit_cast<u32>(std::numeric_limits<f32>::infinity())};
    Writer w;
    for (const u32 b : bits) w.writeF32(std::bit_cast<f32>(b));
    w.writeU32(0x7FC00001u); // a NaN payload, written without a float round trip
    Reader r(w.data());
    for (const u32 b : bits) REQUIRE(std::bit_cast<u32>(r.readF32()) == b);
    REQUIRE(r.readU32() == 0x7FC00001u);
    REQUIRE(r.empty());
}

TEST_CASE("readBytes returns a window and advances") {
    const std::vector<u8> bytes = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE};
    Reader r(bytes);
    r.skip(1);
    const auto window = r.readBytes(3);
    REQUIRE(window.size() == 3);
    REQUIRE(window[0] == 0xBB);
    REQUIRE(window[2] == 0xDD);
    REQUIRE(r.position() == 4);
    REQUIRE(r.readU8() == 0xEE);
    REQUIRE(r.empty());
}

TEST_CASE("truncated input throws located FormatError") {
    const std::vector<u8> empty{};
    expectTruncation(empty, 0, [](Reader& r) { (void)r.readU8(); });

    const std::vector<u8> oneByte = {0x01};
    expectTruncation(oneByte, 0, [](Reader& r) { (void)r.readU16(); });
    expectTruncation(oneByte, 0, [](Reader& r) { (void)r.readU32(); });
    expectTruncation(oneByte, 0, [](Reader& r) { (void)r.readU64(); });

    const std::vector<u8> threeBytes = {0x01, 0x02, 0x03};
    expectTruncation(threeBytes, 0, [](Reader& r) { (void)r.readU32(); });
    expectTruncation(threeBytes, 0, [](Reader& r) { (void)r.readColor(); });

    const std::vector<u8> fiveBytes = {0, 0, 0, 0, 0};
    expectTruncation(fiveBytes, 0, [](Reader& r) { (void)r.readU64(); });
    expectTruncation(fiveBytes, 0, [](Reader& r) { (void)r.readBytes(6); });

    // A reader positioned exactly at the end reports the end position.
    const std::vector<u8> fourBytes = {0, 0, 0, 0};
    expectTruncation(fourBytes, 4, [](Reader& r) {
        r.skip(4);
        (void)r.readU8();
    });
}

TEST_CASE("skip beyond the end throws") {
    const std::vector<u8> bytes = {1, 2, 3};
    Reader r(bytes);
    r.skip(3);
    REQUIRE(r.empty());
    bool threw = false;
    try {
        r.skip(1);
    } catch (const FormatError& error) {
        threw = true;
        REQUIRE(error.offset == 3);
    }
    REQUIRE(threw);
}

TEST_CASE("bounded reader reports absolute offsets") {
    std::vector<u8> bytes(40, 0xAA);
    Reader r(bytes, 20, 30);
    REQUIRE(r.position() == 20);
    REQUIRE(r.remaining() == 10);
    REQUIRE(r.readU8() == 0xAA);
    r.skip(9); // positions 21..29, at the bound
    REQUIRE(r.remaining() == 0);
    bool threw = false;
    try {
        (void)r.readU8();
    } catch (const FormatError& error) {
        threw = true;
        REQUIRE(error.offset == 30); // the bound, not a section-relative 10
    }
    REQUIRE(threw);
}

TEST_CASE("invalid reader bounds throw") {
    std::vector<u8> bytes(10, 0);
    bool threw = false;
    try {
        Reader r(bytes, 5, 3); // pos > end
        (void)r;
    } catch (const FormatError& error) {
        threw = true;
        REQUIRE(error.offset == 5);
    }
    REQUIRE(threw);
    threw = false;
    try {
        Reader r(bytes, 0, 11); // end > size
        (void)r;
    } catch (const FormatError&) {
        threw = true;
    }
    REQUIRE(threw);
}

TEST_CASE("writer pads to alignment") {
    Writer w;
    REQUIRE(w.size() == 0);
    w.padTo(4);
    REQUIRE(w.size() == 0); // already aligned
    w.writeU8(1);
    w.padTo(4);
    REQUIRE(w.size() == 4);
    REQUIRE(w.data()[0] == 1);
    REQUIRE(w.data()[1] == 0);
    REQUIRE(w.data()[3] == 0);
}

TEST_CASE("format constants match spec section 2") {
    REQUIRE(kMagic[0] == 0x49);
    REQUIRE(kMagic[1] == 0x50);
    REQUIRE(kMagic[2] == 0x52);
    REQUIRE(kMagic[3] == 0x50);
    REQUIRE(kHeaderSize == 20);
    REQUIRE(static_cast<u32>(SectionId::Meta) == 1);
    REQUIRE(static_cast<u32>(SectionId::Const) == 2);
    REQUIRE(static_cast<u32>(SectionId::Code) == 3);
    REQUIRE(static_cast<u32>(SectionId::Debug) == 4);
    REQUIRE(static_cast<u8>(InputType::Color) == 0);
    REQUIRE(static_cast<u8>(InputType::F32) == 1);
    REQUIRE(static_cast<u8>(InputType::U32) == 2);
    REQUIRE(static_cast<u8>(OutputKind::Point) == 0);
    REQUIRE(static_cast<u8>(OutputKind::Rect) == 1);
    REQUIRE(static_cast<u8>(OutputKind::Circle) == 2);
}
