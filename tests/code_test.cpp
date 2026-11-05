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
    REQUIRE(code.entry().params.empty());
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
    // One function entry at 4-21: name 4-7, offset 8-11, length 12-15, then
    // param/result/local counts at 16/18/20 (types follow each count).
    const auto oneFunction = [](Writer& w, u32 name, u32 length) {
        w.writeU32(1); w.writeU32(name); w.writeU32(0); w.writeU32(length);
        w.writeU16(0); w.writeU16(0); w.writeU16(0);
    };
    // One part after that entry: count 22-25, function 26-29, size 30-37, origin 38-45.
    const auto onePart = [](Writer& w, u32 function, u32 sizeX) {
        w.writeU32(1); w.writeU32(function); w.writeU32(sizeX); w.writeU32(8); w.writeF32(0); w.writeF32(0);
    };

    for (const u32 count : {0u, static_cast<u32>(kMaxFunctions + 1)}) {
        Writer w; w.writeU32(count);
        expectCodeError(w.data(), pool, 0, "function count");
    }
    { // Unknown parameter type: paramCount 16-17, first type byte at 18.
        Writer w; w.writeU32(1); w.writeU32(0); w.writeU32(0); w.writeU32(0); w.writeU16(1); w.writeU8(9);
        expectCodeError(w.data(), pool, 18, "parameter type");
    }
    { // Unknown local type (4 is distribution since v0.5) at 22.
        Writer w; w.writeU32(1); w.writeU32(0); w.writeU32(0); w.writeU32(0); w.writeU16(0); w.writeU16(0); w.writeU16(1); w.writeU8(5);
        expectCodeError(w.data(), pool, 22, "local type");
    }
    { Writer w; oneFunction(w, 99, 0); expectCodeError(w.data(), pool, 4, "outside"); }  // name outside the pool
    { Writer w; oneFunction(w, 0, 2); expectCodeError(w.data(), pool, 4, "aligned"); }   // length not a multiple of 4
    { // Stream end 22 + 8 exceeds the 22-byte section.
        Writer w; oneFunction(w, 0, 8); expectCodeError(w.data(), pool, kEntryBase + 4, "exceed");
    }
    { // First stream not at offset 0: the contiguity error is at the entry start.
        auto bytes = codeSection({drawSpec({0x09, 0, 0, 0})}, {});
        bytes[8] = 4;
        expectCodeError(bytes, pool, 4, "contiguous");
    }
    { // A gap between two streams: the second entry starts at 22.
        auto bytes = codeSection({drawSpec({0x09, 0, 0, 0}), {kAnonymousName, {}, {}, {}, {0x09, 0, 0, 0}}}, {});
        bytes[26] = 8;
        expectCodeError(bytes, pool, 22, "contiguous");
    }
    { Writer w; oneFunction(w, 0, 0); onePart(w, 5, 8); expectCodeError(w.data(), pool, 26, "part function"); }
    { Writer w; oneFunction(w, 0, 0); onePart(w, 0, 0); expectCodeError(w.data(), pool, 34, "positive"); }
    { // Unknown part input type: inputCount 46-47, first type byte at 48.
        Writer w; oneFunction(w, 0, 0); onePart(w, 0, 8); w.writeU16(1); w.writeU8(7);
        expectCodeError(w.data(), pool, 48, "input type");
    }
    { // Part output reserved byte: the draw entry's part output entry is at 56-59.
        auto bytes = codeSection({drawSpec({0x09, 0, 0, 0})}, {{0, 8, 8, 0, 0, {}, {{0, 0, 1}}}});
        bytes[56] = 0x01;
        expectCodeError(bytes, pool, 56, "reserved");
    }
    { // Trailing bytes after the last part.
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
        CHECK(error.offset == base + 61);
        CHECK(std::string(error.what()).find("duplicate part input") != std::string::npos);
    }
    REQUIRE(threw);
}
