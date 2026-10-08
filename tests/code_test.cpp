#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <string>
#include <vector>

#include "support/test_support.hpp"

#include "iprop/Binary.hpp"
#include "iprop/Code.hpp"
#include "iprop/Format.hpp"
#include "iprop/Meta.hpp"

using namespace iprop;
using namespace test_support;

namespace {

// Sizes of the fixed parts of a function entry with zero param/result/local
// type bytes (name, offset, length, three u16 counts) = 18 bytes.
constexpr std::size_t kEntryBase = 18;

void expectCodeError(const std::vector<u8>& codeBytes, const std::vector<u8>& poolBytes,
                     std::size_t relOffset, const char* messagePart) {
    const auto bytes = fullFile(minimalMeta(), poolBytes, codeBytes);
    const auto layout = parseLayout(bytes);
    const auto pool = parseConsts(bytes, *layout.find(SectionId::Const));
    const std::size_t base = layout.find(SectionId::Code)->offset;
    bool threw = false;
    try {
        (void)parseCode(bytes, *layout.find(SectionId::Code), pool);
    } catch (const FormatError& error) {
        threw = true;
        CHECK(error.offset == base + relOffset);
        CHECK(std::string(error.what()).find(messagePart) != std::string::npos);
    }
    REQUIRE(threw);
}

} // namespace

TEST_CASE("a draw function with a uniform parses") {
    const auto poolBytes = namesPool({"draw", "palette"});
    const std::vector<u8> instructions = {0x10, 0x00, 0x00, 0x00, // const.i32 0
                                          0x16, 0x00, 0x00, 0x00, // drop
                                          0x09, 0x00, 0x00, 0x00}; // return
    const auto codeBytes = codeSection({drawSpec(instructions)}, {});
    const auto bytes = fullFile(minimalMeta(), poolBytes, codeBytes);
    const auto layout = parseLayout(bytes);
    const auto pool = parseConsts(bytes, *layout.find(SectionId::Const));
    const auto code = parseCode(bytes, *layout.find(SectionId::Code), pool);

    REQUIRE(code.functions.size() == 1);
    REQUIRE(code.parts.empty());
    REQUIRE_FALSE(code.entry().isAnonymous());
    REQUIRE(pool.strings[code.entry().nameIndex] == "draw");
    REQUIRE(code.entry().params.size() == 2);
    REQUIRE(code.entry().params[0] == ValueType::I32);
    REQUIRE(code.entry().params[1] == ValueType::I32);
    REQUIRE(code.entry().results.empty());
    REQUIRE(code.entry().locals.empty());
    REQUIRE(code.entry().code.size() == instructions.size());
    REQUIRE(std::equal(code.entry().code.begin(), code.entry().code.end(),
                       instructions.begin()));
}

TEST_CASE("multi-function file with parts parses") {
    const auto poolBytes = namesPool({"draw", "hair", "eyes", "tint", "pupil"});
    const auto codeBytes = codeSection(
        {drawSpec({0x09, 0x00, 0x00, 0x00}),                                   // draw
         {1, {0, 0, 2}, {}, {1, 0}, {0x09, 0x00, 0x00, 0x00}},                    // hair
         {kAnonymousName, {0, 0}, {}, {}, {0x09, 0x00, 0x00, 0x00}}},             // eyes
        {{1, 64, 32, 32.f, 4.f, {{0, 3, {10, 11, 12, 255}, 0, 0}}, {{0, 0, 4}}},
         {2, 16, 8, 8.f, 4.f, {}, {{0, 0, 4}}}});
    const auto bytes = fullFile(minimalMeta(), poolBytes, codeBytes);
    const auto layout = parseLayout(bytes);
    const auto pool = parseConsts(bytes, *layout.find(SectionId::Const));
    const auto code = parseCode(bytes, *layout.find(SectionId::Code), pool);

    REQUIRE(code.functions.size() == 3);
    REQUIRE(code.parts.size() == 2);

    REQUIRE(pool.strings[code.functions[1].nameIndex] == "hair");
    REQUIRE(code.functions[1].params.size() == 3); // variant, archetype, tint
    REQUIRE(code.functions[1].params[2] == ValueType::Color);
    REQUIRE(code.functions[1].locals.size() == 2);
    REQUIRE(code.functions[1].locals[0] == ValueType::F32);
    REQUIRE(code.functions[1].locals[1] == ValueType::I32);
    REQUIRE(code.functions[2].isAnonymous());
    REQUIRE(code.functions[2].code.size() == 4);

    REQUIRE(code.parts[0].functionIndex == 1);
    REQUIRE(code.parts[0].sizeX == 64);
    REQUIRE(code.parts[0].sizeY == 32);
    REQUIRE(code.parts[0].origin == Vec2{32.f, 4.f});
    REQUIRE(code.parts[0].inputs.size() == 1);
    REQUIRE(pool.strings[code.parts[0].inputs[0].nameIndex] == "tint");
    REQUIRE(code.parts[0].inputs[0].colorDefault == Color{10, 11, 12, 255});
    REQUIRE(code.parts[0].outputs.size() == 1);
    REQUIRE(code.parts[0].outputs[0].kind == OutputKind::Point);
    REQUIRE_FALSE(code.parts[0].outputs[0].isList);
    REQUIRE(pool.strings[code.parts[0].outputs[0].nameIndex] == "pupil");

    REQUIRE(code.parts[1].functionIndex == 2);
    REQUIRE(code.parts[1].inputs.empty());
    REQUIRE(code.parts[1].outputs.size() == 1);
}

TEST_CASE("invalid code fixtures fail with located errors") {
    const auto pool = namesPool({"draw", "a", "b"});

    // functionCount = 0 (at section offset 0).
    {
        Writer w;
        w.writeU32(0);
        expectCodeError(w.data(), pool, 0, "function count");
    }
    // functionCount above the limit.
    {
        Writer w;
        w.writeU32(kMaxFunctions + 1);
        expectCodeError(w.data(), pool, 0, "function count");
    }
    // Unknown parameter type. Entry: name 4-7, offset 8-11, len 12-15,
    // paramCount 16-17, first type byte at 18.
    {
        Writer w;
        w.writeU32(1); // functionCount
        w.writeU32(0); // name
        w.writeU32(0); // offset
        w.writeU32(0); // length
        w.writeU16(1); // paramCount
        w.writeU8(9);  // bad type
        expectCodeError(w.data(), pool, 18, "parameter type");
    }
    // Unknown local type: pc 16-17, rc 18-19, lc 20-21, type at 22.
    {
        Writer w;
        w.writeU32(1);
        w.writeU32(0);
        w.writeU32(0);
        w.writeU32(0);
        w.writeU16(0); // params
        w.writeU16(0); // results
        w.writeU16(1); // locals
        w.writeU8(5);  // bad local type (4 is distribution since v0.5)
        expectCodeError(w.data(), pool, 22, "local type");
    }
    // Function name outside the pool (name field at 4-7).
    {
        Writer w;
        w.writeU32(1);
        w.writeU32(99);
        w.writeU32(0);
        w.writeU32(0);
        w.writeU16(0);
        w.writeU16(0);
        w.writeU16(0);
        expectCodeError(w.data(), pool, 4, "outside");
    }
    // First stream not at offset 0: patch the offset field (8-11) to 4;
    // contiguity error at the entry start (4).
    {
        auto bytes = codeSection({drawSpec({0x09, 0, 0, 0})}, {});
        bytes[8] = 4; // offset 4 instead of 0
        expectCodeError(bytes, pool, 4, "contiguous");
    }

    // A gap between two streams: function 1 entry starts at 4 + 20 = 24
    // (draw entry = 18 + 2 param type bytes); its offset field at 28.
    {
        FuncSpec a = drawSpec({0x09, 0, 0, 0});
        FuncSpec b = {kAnonymousName, {}, {}, {}, {0x09, 0, 0, 0}};
        auto bytes = codeSection({a, b}, {});
        bytes[28] = 8; // offset 8 instead of the contiguous 4
        expectCodeError(bytes, pool, 24, "contiguous");
    }
    // Stream length not a multiple of 4 (length field at 12-15).
    {
        Writer w;
        w.writeU32(1);
        w.writeU32(0);
        w.writeU32(0);
        w.writeU32(2); // length 2
        w.writeU16(0);
        w.writeU16(0);
        w.writeU16(0);
        expectCodeError(w.data(), pool, 4, "aligned");
    }
    // Streams exceeding the section: entry 4-21 (18 bytes), streamBase 22,
    // region end 22 + 8 > section size 22 -> error at the section end.
    {
        Writer w;
        w.writeU32(1);
        w.writeU32(0);
        w.writeU32(0);
        w.writeU32(8); // length 8, but no instruction bytes follow
        w.writeU16(0);
        w.writeU16(0);
        w.writeU16(0);
        expectCodeError(w.data(), pool, kEntryBase + 4, "exceed");
    }
    // Part function index out of range. Entry 4-21, partCount 22-25,
    // part funcIdx 26-29 -> error at 26.
    {
        Writer w;
        w.writeU32(1);
        w.writeU32(0);
        w.writeU32(0);
        w.writeU32(0);
        w.writeU16(0);
        w.writeU16(0);
        w.writeU16(0);
        w.writeU32(1); // partCount
        w.writeU32(5); // function index 5 >= 1
        w.writeU32(8);
        w.writeU32(8);
        w.writeF32(0);
        w.writeF32(0);
        expectCodeError(w.data(), pool, 26, "part function");
    }
    // Part canvas size zero: sizeX 30-33, sizeY 34-37; error after both at 34.
    {
        Writer w;
        w.writeU32(1);
        w.writeU32(0);
        w.writeU32(0);
        w.writeU32(0);
        w.writeU16(0);
        w.writeU16(0);
        w.writeU16(0);
        w.writeU32(1);
        w.writeU32(0);
        w.writeU32(0); // sizeX = 0
        w.writeU32(8);
        w.writeF32(0);
        w.writeF32(0);
        expectCodeError(w.data(), pool, 34, "positive");
    }
    // Part input type unknown: funcIdx 26-29, size 30-37, origin 38-45,
    // inputCount 46-47, first type byte at 48.
    {
        Writer w;
        w.writeU32(1);
        w.writeU32(0);
        w.writeU32(0);
        w.writeU32(0);
        w.writeU16(0);
        w.writeU16(0);
        w.writeU16(0);
        w.writeU32(1);
        w.writeU32(0);
        w.writeU32(8);
        w.writeU32(8);
        w.writeF32(0);
        w.writeF32(0);
        w.writeU16(1); // inputCount
        w.writeU8(7);  // bad input type
        expectCodeError(w.data(), pool, 48, "input type");
    }
    // Part output reserved byte non-zero. With the codeSection builder and a
    // draw function: part starts at 32, inCount 52-53, outCount 54-55,
    // output entry: kind 56, list 57, reserved 58-59.
    {
        auto bytes = codeSection({drawSpec({0x09, 0, 0, 0})},
                                 {{0, 8, 8, 0, 0, {}, {{0, 0, 1}}}});
        bytes[58] = 0x01;
        expectCodeError(bytes, pool, 58, "reserved");
    }
    // Trailing bytes after the last part.
    {
        auto bytes = codeSection({drawSpec({0x09, 0, 0, 0})}, {});
        bytes.push_back(0xEE);
        expectCodeError(bytes, pool, bytes.size() - 1, "trailing");
    }
}

TEST_CASE("duplicate part input names fail at the second entry") {
    const auto pool = namesPool({"draw", "tint"});
    const auto codeBytes = codeSection(
        {drawSpec({0x09, 0, 0, 0})},
        {{0, 8, 8, 0, 0, {{0, 1, {}, 0, 0}, {0, 1, {}, 0, 0}}, {}}});
    const auto bytes = fullFile(minimalMeta(), pool, codeBytes);
    const auto layout = parseLayout(bytes);
    const auto base = layout.find(SectionId::Code)->offset;
    // Part at 32: funcIdx 32-35, size 36-43, origin 44-51, inCount 52-53,
    // first input entry 54-62, second entry starts at 63.
    bool threw = false;
    try {
        const auto parsedPool = parseConsts(bytes, *layout.find(SectionId::Const));
        (void)parseCode(bytes, *layout.find(SectionId::Code), parsedPool);
    } catch (const FormatError& error) {
        threw = true;
        CHECK(error.offset == base + 63);
        CHECK(std::string(error.what()).find("duplicate part input") != std::string::npos);
    }
    REQUIRE(threw);
}
