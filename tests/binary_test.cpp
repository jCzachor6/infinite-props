#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <cstring>
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

} // namespace

TEST_CASE("u8 round-trips") {
    Writer w;
    w.writeU8(0);
    w.writeU8(0xFF);
    w.writeU8(0x80);
    Reader r(w.data());
    REQUIRE(r.readU8() == 0);
    REQUIRE(r.readU8() == 0xFF);
    REQUIRE(r.readU8() == 0x80);
    REQUIRE(r.empty());
}

TEST_CASE("u16 round-trips with the high bit set") {
    Writer w;
    w.writeU16(0);
    w.writeU16(0xFFFF);
    w.writeU16(0x8001);
    w.writeU16(0x0100); // checks byte order
    Reader r(w.data());
    REQUIRE(r.readU16() == 0);
    REQUIRE(r.readU16() == 0xFFFF);
    REQUIRE(r.readU16() == 0x8001);
    REQUIRE(r.readU16() == 0x0100);
    REQUIRE(r.empty());
}

TEST_CASE("u32 round-trips") {
    Writer w;
    w.writeU32(0);
    w.writeU32(0xFFFFFFFFu);
    w.writeU32(0x12345678u);
    w.writeU32(0x00000100u); // little-endian byte order
    Reader r(w.data());
    REQUIRE(r.readU32() == 0);
    REQUIRE(r.readU32() == 0xFFFFFFFFu);
    REQUIRE(r.readU32() == 0x12345678u);
    REQUIRE(r.readU32() == 0x00000100u);
    REQUIRE(r.empty());
}

TEST_CASE("u64 round-trips") {
    Writer w;
    w.writeU64(0);
    w.writeU64(0xFFFFFFFFFFFFFFFFull);
    w.writeU64(0x0102030405060708ull);
    Reader r(w.data());
    REQUIRE(r.readU64() == 0);
    REQUIRE(r.readU64() == 0xFFFFFFFFFFFFFFFFull);
    REQUIRE(r.readU64() == 0x0102030405060708ull);
    REQUIRE(r.empty());
}

TEST_CASE("i32 round-trips negative values") {
    Writer w;
    w.writeI32(0);
    w.writeI32(-1);
    w.writeI32(std::numeric_limits<i32>::min());
    w.writeI32(std::numeric_limits<i32>::max());
    Reader r(w.data());
    REQUIRE(r.readI32() == 0);
    REQUIRE(r.readI32() == -1);
    REQUIRE(r.readI32() == std::numeric_limits<i32>::min());
    REQUIRE(r.readI32() == std::numeric_limits<i32>::max());
    REQUIRE(r.empty());
}

TEST_CASE("f32 round-trips preserve bit patterns exactly") {
    const std::array<f32, 6> values = {
        0.f, -0.f, 1.5f, -2.25f,
        std::numeric_limits<f32>::min(),      // smallest normal
        std::numeric_limits<f32>::infinity(),
    };
    Writer w;
    for (const f32 v : values) {
        w.writeF32(v);
    }
    // A NaN with a specific payload must survive bit-exactly.
    const u32 nanBits = 0x7FC00001u;
    w.writeU32(nanBits);

    Reader r(w.data());
    for (const f32 v : values) {
        u32 expectedBits = 0;
        std::memcpy(&expectedBits, &v, sizeof(expectedBits));
        u32 readBits = 0;
        const f32 read = r.readF32();
        std::memcpy(&readBits, &read, sizeof(readBits));
        REQUIRE(readBits == expectedBits);
    }
    REQUIRE(r.readU32() == nanBits);
    REQUIRE(r.empty());
}

TEST_CASE("color round-trips") {
    Writer w;
    w.writeColor({0, 0, 0, 0});
    w.writeColor({255, 255, 255, 255});
    w.writeColor({1, 2, 3, 4});
    Reader r(w.data());
    REQUIRE(r.readColor() == Color{0, 0, 0, 0});
    REQUIRE(r.readColor() == Color{255, 255, 255, 255});
    REQUIRE(r.readColor() == Color{1, 2, 3, 4});
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

TEST_CASE("opcode values match spec section 9 exactly") {
    // The full table from spec/iprop-v0.1.md §9. Any divergence is a frozen
    // contract break. std::to_array deduces the exact entry count: an extra
    // or missing entry would change the type and fail to compile.
    const auto table = std::to_array<std::pair<Opcode, u8>>({
        {Opcode::Nop, 0x00}, {Opcode::Block, 0x01}, {Opcode::Loop, 0x02},
        {Opcode::If, 0x03}, {Opcode::Else, 0x04}, {Opcode::End, 0x05},
        {Opcode::Br, 0x06}, {Opcode::BrIf, 0x07}, {Opcode::Call, 0x08},
        {Opcode::Return, 0x09},

        {Opcode::ConstI32, 0x10}, {Opcode::ConstF32, 0x11},
        {Opcode::ConstColor, 0x12}, {Opcode::LocalGet, 0x13},
        {Opcode::LocalSet, 0x14}, {Opcode::LocalTee, 0x15},
        {Opcode::Drop, 0x16}, {Opcode::Dup, 0x17},

        {Opcode::I32Add, 0x20}, {Opcode::I32Sub, 0x21}, {Opcode::I32Mul, 0x22},
        {Opcode::I32DivS, 0x23}, {Opcode::I32DivU, 0x24},
        {Opcode::I32RemS, 0x25}, {Opcode::I32RemU, 0x26},
        {Opcode::I32And, 0x27}, {Opcode::I32Or, 0x28}, {Opcode::I32Xor, 0x29},
        {Opcode::I32Shl, 0x2A}, {Opcode::I32ShrS, 0x2B},
        {Opcode::I32ShrU, 0x2C}, {Opcode::I32Neg, 0x2D},
        {Opcode::I32Not, 0x2E}, {Opcode::I32Eq, 0x30}, {Opcode::I32Ne, 0x31},
        {Opcode::I32LtS, 0x32}, {Opcode::I32LtU, 0x33}, {Opcode::I32GtS, 0x34},
        {Opcode::I32GtU, 0x35}, {Opcode::I32LeS, 0x36},
        {Opcode::I32LeU, 0x37}, {Opcode::I32GeS, 0x38},
        {Opcode::I32GeU, 0x39},

        {Opcode::F32Add, 0x40}, {Opcode::F32Sub, 0x41}, {Opcode::F32Mul, 0x42},
        {Opcode::F32Div, 0x43}, {Opcode::F32Eq, 0x44}, {Opcode::F32Ne, 0x45},
        {Opcode::F32Lt, 0x46}, {Opcode::F32Gt, 0x47}, {Opcode::F32Le, 0x48},
        {Opcode::F32Ge, 0x49}, {Opcode::F32Neg, 0x4A},
        {Opcode::F32Abs, 0x4B}, {Opcode::F32Sqrt, 0x4C},
        {Opcode::I32ToF32, 0x4D}, {Opcode::F32ToI32, 0x4E},

        {Opcode::ColorMix, 0x50}, {Opcode::ColorShade, 0x51},
        {Opcode::ColorSetA, 0x52},

        {Opcode::StreamPush, 0x60}, {Opcode::StreamPop, 0x61},
        {Opcode::RngUnit, 0x62}, {Opcode::RngRange, 0x63},
        {Opcode::RngIndex, 0x64}, {Opcode::Hash, 0x65},
        {Opcode::StreamState, 0x66},

        {Opcode::Noise, 0x70}, {Opcode::Fault, 0x71}, {Opcode::Smooth, 0x72},
        {Opcode::Lerp, 0x73},

        {Opcode::Pixel, 0x80}, {Opcode::Rect, 0x81}, {Opcode::Line, 0x82},
        {Opcode::Poly, 0x83}, {Opcode::Oval, 0x84}, {Opcode::Weather, 0x85},

        {Opcode::OutPoint, 0x90}, {Opcode::OutRect, 0x91},
        {Opcode::OutCircle, 0x92}, {Opcode::OutListPoint, 0x93},
        {Opcode::OutListRect, 0x94}, {Opcode::OutListCircle, 0x95},
        {Opcode::PartGetPoint, 0x96}, {Opcode::PartGetRect, 0x97},
        {Opcode::PartGetCircle, 0x98},

        {Opcode::PartDraw, 0xA0},
    });

    // The table must contain every opcode exactly once (sanity on top of the
    // value check: 87 opcodes in v0.1).
    REQUIRE(table.size() == 88);

    for (const auto& [opcode, expected] : table) {
        INFO("opcode " << static_cast<unsigned>(expected));
        REQUIRE(static_cast<u8>(opcode) == expected);
    }

    // Uniqueness: the frozen contract must not reuse values.
    std::array<bool, 256> seen{};
    for (const auto& [opcode, value] : table) {
        REQUIRE_FALSE(seen[value]);
        seen[value] = true;
    }
}

TEST_CASE("format constants match spec section 2") {
    REQUIRE(kMagic[0] == 0x49);
    REQUIRE(kMagic[1] == 0x50);
    REQUIRE(kMagic[2] == 0x52);
    REQUIRE(kMagic[3] == 0x50);
    REQUIRE(kHeaderSize == 20);
    REQUIRE(kVersionMajor == 0);
    REQUIRE(kVersionMinor == 3);
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
