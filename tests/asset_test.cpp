#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <limits>
#include <string>

#include "support/test_support.hpp"

#include "iprop/Consts.hpp"
#include "iprop/Format.hpp"
#include "iprop/Meta.hpp"

using namespace iprop;
using namespace test_support;

TEST_CASE("const section round-trips all pools") {
    const auto poolBytes = constSection(
        {"rock", "", "żółć", "\xF0\x9F\x9A\x80"},
        {0, -1, std::numeric_limits<i32>::max(), std::numeric_limits<i32>::min()},
        {0.5f, -2.25f, 0.f},
        {{1, 2, 3, 4}, {255, 255, 255, 255}});
    const auto bytes = fullFile({}, poolBytes);
    const auto layout = parseLayout(bytes);
    const auto pool = parseConsts(bytes, *layout.find(SectionId::Const));

    REQUIRE(pool.strings.size() == 4);
    REQUIRE(pool.strings[0] == "rock");
    REQUIRE(pool.strings[1].empty());
    REQUIRE(pool.strings[2] == "żółć");
    REQUIRE(pool.strings[3] == "\xF0\x9F\x9A\x80");

    REQUIRE(pool.ints.size() == 4);
    REQUIRE(pool.ints[1] == -1);
    REQUIRE(pool.ints[2] == std::numeric_limits<i32>::max());
    REQUIRE(pool.ints[3] == std::numeric_limits<i32>::min());

    REQUIRE(pool.floats.size() == 3);
    REQUIRE(pool.floats[0] == 0.5f);
    REQUIRE(pool.floats[1] == -2.25f);

    REQUIRE(pool.colors.size() == 2);
    REQUIRE(pool.colors[0] == Color{1, 2, 3, 4});
    REQUIRE(pool.colors[1] == Color{255, 255, 255, 255});

    // The i32 pool also serves as the u32 pool (spec §5).
    REQUIRE(static_cast<u32>(pool.ints[1]) == 0xFFFFFFFFu);
}

TEST_CASE("f32 NaN payload survives the const pool bit-exactly") {
    const u32 nanBits = 0x7FC00001u;
    f32 nan = 0.f;
    std::memcpy(&nan, &nanBits, sizeof(nan));
    const auto bytes = fullFile({}, constSection({}, {}, {nan}, {}));
    const auto layout = parseLayout(bytes);
    const auto pool = parseConsts(bytes, *layout.find(SectionId::Const));

    REQUIRE(pool.floats.size() == 1);
    u32 readBits = 0;
    std::memcpy(&readBits, &pool.floats[0], sizeof(readBits));
    REQUIRE(readBits == nanBits);
}

TEST_CASE("meta without emission parses completely") {
    const auto poolBytes = namesPool({"palette", "scale", "count", "collider", "anchors"});
    const auto metaBytes = metaSection(192, 192, 96.f, 170.f, 0x123456789ABull, false,
                                       {}, 0, 0, 0,
                                       {{0, 0, {10, 20, 30, 255}, 0, 0},
                                        {1, 1, {}, 2.5f, 0},
                                        {2, 2, {}, 0, 7}},
                                       {{0, 0, 3}, {1, 1, 4}});
    const auto bytes = fullFile(metaBytes, poolBytes);
    const auto layout = parseLayout(bytes);
    const auto pool = parseConsts(bytes, *layout.find(SectionId::Const));
    const auto meta = parseMeta(bytes, *layout.find(SectionId::Meta), pool);

    REQUIRE(meta.sizeX == 192);
    REQUIRE(meta.sizeY == 192);
    REQUIRE(meta.origin == Vec2{96.f, 170.f});
    REQUIRE(meta.fuel == 0x123456789ABull);
    REQUIRE_FALSE(meta.hasEmission);

    REQUIRE(meta.inputs.size() == 3);
    REQUIRE(meta.inputs[0].type == InputType::Color);
    REQUIRE(meta.inputs[0].colorDefault == Color{10, 20, 30, 255});
    REQUIRE(meta.inputs[1].type == InputType::F32);
    REQUIRE(meta.inputs[1].f32Default == 2.5f);
    REQUIRE(meta.inputs[2].type == InputType::U32);
    REQUIRE(meta.inputs[2].u32Default == 7);

    REQUIRE(meta.outputs.size() == 2);
    REQUIRE(meta.outputs[0].kind == OutputKind::Point);
    REQUIRE_FALSE(meta.outputs[0].isList);
    REQUIRE(meta.outputs[1].kind == OutputKind::Rect);
    REQUIRE(meta.outputs[1].isList);

    REQUIRE(meta.findInput("palette", pool) == &meta.inputs[0]);
    REQUIRE(meta.findInput("scale", pool) == &meta.inputs[1]);
    REQUIRE(meta.findInput("missing", pool) == nullptr);
    REQUIRE(meta.findOutput("collider", pool) == &meta.outputs[0]);
    REQUIRE(meta.findOutput("anchors", pool) == &meta.outputs[1]);
    REQUIRE(meta.findOutput("missing", pool) == nullptr);
}

TEST_CASE("meta with emission parses the emission block") {
    const auto poolBytes = namesPool({});
    const auto metaBytes = metaSection(64, 64, 0, 0, 1000, true,
                                       {255, 83, 8, 255}, 0.f, -30.f, 7.f, {}, {});
    const auto bytes = fullFile(metaBytes, poolBytes);
    const auto layout = parseLayout(bytes);
    const auto pool = parseConsts(bytes, *layout.find(SectionId::Const));
    const auto meta = parseMeta(bytes, *layout.find(SectionId::Meta), pool);

    REQUIRE(meta.hasEmission);
    REQUIRE(meta.emission.color == Color{255, 83, 8, 255});
    REQUIRE(meta.emission.offset == Vec2{0.f, -30.f});
    REQUIRE(meta.emission.radius == 7.f);
}

TEST_CASE("meta with emission, inputs and outputs combined") {
    // Exercises the inputCount-at-56 path (spec §4): emission block present
    // and followed by a full I/O signature.
    const auto poolBytes = namesPool({"tint", "weight", "flags", "hit", "glow"});
    const auto metaBytes = metaSection(128, 96, 64.f, 80.f, 5000, true,
                                       {10, 200, 40, 255}, 0.f, -12.f, 5.5f,
                                       {{0, 0, {1, 2, 3, 255}, 0, 0},
                                        {1, 1, {}, 0.25f, 0},
                                        {2, 2, {}, 0, 9}},
                                       {{0, 0, 3}, {2, 1, 4}});
    const auto bytes = fullFile(metaBytes, poolBytes);
    const auto layout = parseLayout(bytes);
    const auto pool = parseConsts(bytes, *layout.find(SectionId::Const));
    const auto meta = parseMeta(bytes, *layout.find(SectionId::Meta), pool);

    REQUIRE(meta.hasEmission);
    REQUIRE(meta.emission.color == Color{10, 200, 40, 255});
    REQUIRE(meta.emission.offset == Vec2{0.f, -12.f});
    REQUIRE(meta.emission.radius == 5.5f);
    REQUIRE(meta.inputs.size() == 3);
    REQUIRE(meta.inputs[0].type == InputType::Color);
    REQUIRE(meta.inputs[0].colorDefault == Color{1, 2, 3, 255});
    REQUIRE(meta.inputs[1].type == InputType::F32);
    REQUIRE(meta.inputs[1].f32Default == 0.25f);
    REQUIRE(meta.inputs[2].type == InputType::U32);
    REQUIRE(meta.inputs[2].u32Default == 9);
    REQUIRE(meta.outputs.size() == 2);
    REQUIRE(meta.outputs[0].kind == OutputKind::Point);
    REQUIRE(meta.outputs[1].kind == OutputKind::Circle);
    REQUIRE(meta.outputs[1].isList);
    REQUIRE(meta.findInput("tint", pool) == &meta.inputs[0]);
    REQUIRE(meta.findOutput("glow", pool) == &meta.outputs[1]);
}

TEST_CASE("root output slots keep declaration order (spec §4.3)") {
    const auto poolBytes = namesPool({"first", "second", "third"});
    const auto metaBytes = metaSection(8, 8, 0, 0, 10, false, {}, 0, 0, 0, {},
                                       {{1, 0, 0}, {0, 0, 1}, {2, 1, 2}});
    const auto bytes = fullFile(metaBytes, poolBytes);
    const auto layout = parseLayout(bytes);
    const auto pool = parseConsts(bytes, *layout.find(SectionId::Const));
    const auto meta = parseMeta(bytes, *layout.find(SectionId::Meta), pool);

    REQUIRE(meta.outputs.size() == 3);
    REQUIRE(meta.outputs[0].kind == OutputKind::Rect);
    REQUIRE(meta.outputs[1].kind == OutputKind::Point);
    REQUIRE(meta.outputs[2].kind == OutputKind::Circle);
    REQUIRE(meta.outputs[2].isList);
    REQUIRE(pool.strings[meta.outputs[0].nameIndex] == "first");
    REQUIRE(pool.strings[meta.outputs[1].nameIndex] == "second");
    REQUIRE(pool.strings[meta.outputs[2].nameIndex] == "third");
}

TEST_CASE("invalid meta fixtures fail with located errors") {
    const auto pool = namesPool({"a", "b"});

    // sizeX = 0 (size block starts at meta offset 0).
    expectMetaError(metaSection(0, 64, 0, 0, 10, false, {}, 0, 0, 0, {}, {}), pool,
                    0, "positive");
    // fuel = 0 (fuel at 16).
    expectMetaError(metaSection(64, 64, 0, 0, 0, false, {}, 0, 0, 0, {}, {}), pool,
                    16, "fuel");
    // Unknown flag bit (flags at 24).
    expectMetaError(metaSection(64, 64, 0, 0, 10, false, {}, 0, 0, 0, {}, {}, 2),
                    pool, 24, "flag");
    // Unknown input type (first input entry: count at 32, entry at 34).
    expectMetaError(metaSection(64, 64, 0, 0, 10, false, {}, 0, 0, 0,
                                {{3, 0, {}, 0, 0}}, {}),
                    pool, 34, "input type");
    // Input name outside the pool (name index at entry+1).
    expectMetaError(metaSection(64, 64, 0, 0, 10, false, {}, 0, 0, 0,
                                {{0, 99, {}, 0, 0}}, {}),
                    pool, 35, "outside");
    // Duplicate input name (second entry starts at 34 + 9).
    expectMetaError(metaSection(64, 64, 0, 0, 10, false, {}, 0, 0, 0,
                                {{0, 0, {}, 0, 0}, {0, 0, {}, 0, 0}}, {}),
                    pool, 43, "duplicate input");
    // Unknown output kind (no inputs: outputCount at 34, entry at 36).
    expectMetaError(metaSection(64, 64, 0, 0, 10, false, {}, 0, 0, 0, {},
                                {{9, 0, 0}}),
                    pool, 36, "output kind");
    // Invalid list flag (flag byte at entry+1).
    expectMetaError(metaSection(64, 64, 0, 0, 10, false, {}, 0, 0, 0, {},
                                {{0, 9, 0}}),
                    pool, 37, "list flag");
    // Output name outside the pool (name at entry+4).
    expectMetaError(metaSection(64, 64, 0, 0, 10, false, {}, 0, 0, 0, {},
                                {{0, 0, 99}}),
                    pool, 40, "outside");
    // Duplicate output name (second entry starts at 36 + 8).
    expectMetaError(metaSection(64, 64, 0, 0, 10, false, {}, 0, 0, 0, {},
                                {{0, 0, 0}, {0, 0, 0}}),
                    pool, 44, "duplicate output");
    // Truncated meta: only 4 bytes — sizeY read fails at offset 4.
    expectMetaError({0, 0, 0, 0}, pool, 4, "end");
}

TEST_CASE("meta reserved bytes must be zero") {
    const auto pool = namesPool({});
    // Flip reserved[1] (offset 27 within the section).
    auto metaBytes = metaSection(64, 64, 0, 0, 10, false, {}, 0, 0, 0, {}, {});
    metaBytes[27] = 0x01;
    expectMetaError(metaBytes, pool, 27, "reserved");
}

TEST_CASE("output entry reserved bytes must be zero") {
    const auto pool = namesPool({"a"});
    // One output: entry at 36; reserved bytes at 38..39.
    auto metaBytes = metaSection(64, 64, 0, 0, 10, false, {}, 0, 0, 0, {},
                                 {{0, 0, 0}});
    metaBytes[39] = 0x01;
    expectMetaError(metaBytes, pool, 39, "reserved");
}

TEST_CASE("trailing bytes in meta fail") {
    const auto pool = namesPool({});
    auto metaBytes = metaSection(64, 64, 0, 0, 10, false, {}, 0, 0, 0, {}, {});
    metaBytes.push_back(0xEE); // trailing garbage after the last output
    expectMetaError(metaBytes, pool, metaBytes.size() - 1, "trailing");
}

TEST_CASE("invalid const fixtures fail with located errors") {
    // String length exceeding the section (len field at 4, bytes at 8).
    {
        Writer w;
        w.writeU32(1);      // stringCount
        w.writeU32(100);    // len
        w.writeU8(0x41);    // only one byte
        expectConstError(w.data(), 4, "exceeds");
    }
    // Invalid UTF-8 byte (bytes start at 8, len = 1 → error at 8).
    {
        Writer w;
        w.writeU32(1);
        w.writeU32(1);
        w.writeU8(0xFF);
        expectConstError(w.data(), 8, "UTF-8");
    }
    // String pool above the limit (count at 0).
    {
        Writer w;
        w.writeU32(kMaxConstPoolEntries + 1);
        expectConstError(w.data(), 0, "string pool too large");
    }
    // Truncated const: nothing after the section start.
    expectConstError({}, 0, "end");
}

TEST_CASE("trailing bytes in const fail") {
    auto poolBytes = constSection({"a"}, {}, {}, {});
    poolBytes.push_back(0xEE);
    expectConstError(poolBytes, poolBytes.size() - 1, "trailing");
}

TEST_CASE("parsers reject sections of the wrong id") {
    const auto poolBytes = namesPool({});
    const auto metaBytes = metaSection(8, 8, 0, 0, 10, false, {}, 0, 0, 0, {}, {});
    const auto bytes = fullFile(metaBytes, poolBytes);
    const auto layout = parseLayout(bytes);

    const auto& meta = *layout.find(SectionId::Meta);
    const auto& consts = *layout.find(SectionId::Const);

    bool threw = false;
    try {
        (void)parseMeta(bytes, consts, ConstPool{});
    } catch (const FormatError& error) {
        threw = true;
        REQUIRE(error.offset == consts.offset);
        REQUIRE(std::string(error.what()).find("not a meta section") != std::string::npos);
    }
    REQUIRE(threw);

    threw = false;
    try {
        (void)parseConsts(bytes, meta);
    } catch (const FormatError& error) {
        threw = true;
        REQUIRE(error.offset == meta.offset);
    }
    REQUIRE(threw);
}
