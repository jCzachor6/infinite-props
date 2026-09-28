#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

#include "iprop/Asset.hpp"
#include "iprop/Binary.hpp"
#include "iprop/Format.hpp"

using namespace iprop;

namespace {

std::vector<u8> constSection(const std::vector<std::string>& strings,
                             const std::vector<i32>& ints,
                             const std::vector<f32>& floats,
                             const std::vector<Color>& colors) {
    Writer w;
    w.writeU32(static_cast<u32>(strings.size()));
    for (const auto& s : strings) {
        w.writeU32(static_cast<u32>(s.size()));
        w.writeBytes({reinterpret_cast<const u8*>(s.data()), s.size()});
    }
    w.writeU32(static_cast<u32>(ints.size()));
    for (const i32 v : ints) {
        w.writeI32(v);
    }
    w.writeU32(static_cast<u32>(floats.size()));
    for (const f32 v : floats) {
        w.writeF32(v);
    }
    w.writeU32(static_cast<u32>(colors.size()));
    for (const Color c : colors) {
        w.writeColor(c);
    }
    return w.data();
}

struct InputSpec {
    u8 type;
    u32 nameIndex;
    Color c{};
    f32 f = 0.f;
    u32 u = 0;
};

struct OutputSpec {
    u8 kind;
    u8 list;
    u32 nameIndex;
};

std::vector<u8> metaSection(u32 sizeX, u32 sizeY, f32 ox, f32 oy, u32 variants,
                            u32 archetypes, u64 fuel, bool emission, Color emColor,
                            f32 emX, f32 emY, f32 emR,
                            const std::vector<InputSpec>& inputs,
                            const std::vector<OutputSpec>& outputs,
                            u16 extraFlags = 0) {
    Writer w;
    w.writeU32(sizeX);
    w.writeU32(sizeY);
    w.writeF32(ox);
    w.writeF32(oy);
    w.writeU32(variants);
    w.writeU32(archetypes);
    w.writeU64(fuel);
    w.writeU16(static_cast<u16>((emission ? 1u : 0u) | extraFlags));
    for (int i = 0; i < 6; ++i) {
        w.writeU8(0); // reserved
    }
    if (emission) {
        w.writeColor(emColor);
        w.writeF32(emX);
        w.writeF32(emY);
        w.writeF32(emR);
    }
    w.writeU16(static_cast<u16>(inputs.size()));
    for (const auto& in : inputs) {
        w.writeU8(in.type);
        w.writeU32(in.nameIndex);
        if (in.type == 0) {
            w.writeColor(in.c);
        } else if (in.type == 1) {
            w.writeF32(in.f);
        } else {
            w.writeU32(in.u);
        }
    }
    w.writeU16(static_cast<u16>(outputs.size()));
    for (const auto& out : outputs) {
        w.writeU8(out.kind);
        w.writeU8(out.list);
        w.writeU8(0);
        w.writeU8(0);
        w.writeU32(out.nameIndex);
    }
    return w.data();
}

// A complete valid file: meta, const, code sections in id order.
std::vector<u8> fullFile(const std::vector<u8>& meta, const std::vector<u8>& pool,
                         const std::vector<u8>& code = {}) {
    Writer w;
    for (const u8 m : kMagic) {
        w.writeU8(m);
    }
    w.writeU16(kVersionMajor);
    w.writeU16(kVersionMinor);
    w.writeU32(0);
    w.writeU32(3);
    w.writeU32(0);
    u32 off = kHeaderSize + 3 * 12;
    w.writeU32(1);
    w.writeU32(off);
    w.writeU32(static_cast<u32>(meta.size()));
    off += static_cast<u32>(meta.size());
    w.writeU32(2);
    w.writeU32(off);
    w.writeU32(static_cast<u32>(pool.size()));
    off += static_cast<u32>(pool.size());
    w.writeU32(3);
    w.writeU32(off);
    w.writeU32(static_cast<u32>(code.size()));
    off += static_cast<u32>(code.size());
    w.writeBytes(meta);
    w.writeBytes(pool);
    w.writeBytes(code);
    return w.data();
}

// A const pool with the given names, enough for meta fixtures.
std::vector<u8> namesPool(const std::vector<std::string>& names) {
    return constSection(names, {}, {}, {});
}

void expectMetaError(const std::vector<u8>& metaBytes, const std::vector<u8>& poolBytes,
                     std::size_t relOffset, const char* messagePart) {
    const auto bytes = fullFile(metaBytes, poolBytes);
    const auto layout = parseLayout(bytes);
    const auto pool = parseConsts(bytes, *layout.find(SectionId::Const));
    const std::size_t base = layout.find(SectionId::Meta)->offset;
    bool threw = false;
    try {
        (void)parseMeta(bytes, *layout.find(SectionId::Meta), pool);
    } catch (const FormatError& error) {
        threw = true;
        CHECK(error.offset == base + relOffset);
        CHECK(std::string(error.what()).find(messagePart) != std::string::npos);
    }
    REQUIRE(threw);
}

void expectConstError(const std::vector<u8>& poolBytes, std::size_t relOffset,
                      const char* messagePart) {
    const auto bytes = fullFile({}, poolBytes);
    const auto layout = parseLayout(bytes);
    const std::size_t base = layout.find(SectionId::Const)->offset;
    bool threw = false;
    try {
        (void)parseConsts(bytes, *layout.find(SectionId::Const));
    } catch (const FormatError& error) {
        threw = true;
        CHECK(error.offset == base + relOffset);
        CHECK(std::string(error.what()).find(messagePart) != std::string::npos);
    }
    REQUIRE(threw);
}

} // namespace

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

TEST_CASE("meta without emission parses completely") {
    const auto poolBytes = namesPool({"palette", "scale", "count", "collider", "anchors"});
    const auto metaBytes = metaSection(192, 192, 96.f, 170.f, 5, 4, 0x123456789ABull, false,
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
    REQUIRE(meta.variants == 5);
    REQUIRE(meta.archetypes == 4);
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
    const auto metaBytes = metaSection(64, 64, 0, 0, 1, 1, 1000, true,
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

TEST_CASE("root output slots keep declaration order (spec §4.3)") {
    const auto poolBytes = namesPool({"first", "second", "third"});
    const auto metaBytes = metaSection(8, 8, 0, 0, 1, 1, 10, false, {}, 0, 0, 0, {},
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
    expectMetaError(metaSection(0, 64, 0, 0, 1, 1, 10, false, {}, 0, 0, 0, {}, {}), pool,
                    0, "positive");
    // variants = 0 (variants block at 16).
    expectMetaError(metaSection(64, 64, 0, 0, 0, 1, 10, false, {}, 0, 0, 0, {}, {}), pool,
                    16, "at least 1");
    // fuel = 0 (fuel at 24).
    expectMetaError(metaSection(64, 64, 0, 0, 1, 1, 0, false, {}, 0, 0, 0, {}, {}), pool,
                    24, "fuel");
    // Unknown flag bit (flags at 32).
    expectMetaError(metaSection(64, 64, 0, 0, 1, 1, 10, false, {}, 0, 0, 0, {}, {}, 2),
                    pool, 32, "flag");
    // Unknown input type (first input entry: count at 40, entry at 42).
    expectMetaError(metaSection(64, 64, 0, 0, 1, 1, 10, false, {}, 0, 0, 0,
                                {{3, 0, {}, 0, 0}}, {}),
                    pool, 42, "input type");
    // Input name outside the pool (name index at entry+1).
    expectMetaError(metaSection(64, 64, 0, 0, 1, 1, 10, false, {}, 0, 0, 0,
                                {{0, 99, {}, 0, 0}}, {}),
                    pool, 43, "outside");
    // Duplicate input name (second entry starts at 42 + 9).
    expectMetaError(metaSection(64, 64, 0, 0, 1, 1, 10, false, {}, 0, 0, 0,
                                {{0, 0, {}, 0, 0}, {0, 0, {}, 0, 0}}, {}),
                    pool, 51, "duplicate input");
    // Unknown output kind (no inputs: outputCount at 42, entry at 44).
    expectMetaError(metaSection(64, 64, 0, 0, 1, 1, 10, false, {}, 0, 0, 0, {},
                                {{9, 0, 0}}),
                    pool, 44, "output kind");
    // Invalid list flag (flag byte at entry+1).
    expectMetaError(metaSection(64, 64, 0, 0, 1, 1, 10, false, {}, 0, 0, 0, {},
                                {{0, 9, 0}}),
                    pool, 45, "list flag");
    // Output name outside the pool (name at entry+4).
    expectMetaError(metaSection(64, 64, 0, 0, 1, 1, 10, false, {}, 0, 0, 0, {},
                                {{0, 0, 99}}),
                    pool, 48, "outside");
    // Duplicate output name (second entry starts at 44 + 8).
    expectMetaError(metaSection(64, 64, 0, 0, 1, 1, 10, false, {}, 0, 0, 0, {},
                                {{0, 0, 0}, {0, 0, 0}}),
                    pool, 52, "duplicate output");
    // Truncated meta: only 4 bytes — sizeY read fails at offset 4.
    expectMetaError({0, 0, 0, 0}, pool, 4, "end");
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

TEST_CASE("parsers reject sections of the wrong id") {
    const auto poolBytes = namesPool({});
    const auto metaBytes = metaSection(8, 8, 0, 0, 1, 1, 10, false, {}, 0, 0, 0, {}, {});
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
