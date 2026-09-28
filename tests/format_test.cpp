#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <initializer_list>
#include <span>
#include <vector>

#include "iprop/Binary.hpp"
#include "iprop/Constants.hpp"
#include "iprop/Format.hpp"

using namespace iprop;

namespace {

// A valid file: header + table + payload bytes. Payload sizes may be zero.
std::vector<u8> makeLayout(const std::vector<std::pair<SectionId, std::size_t>>& defs,
                           u32 flags = 0, u16 minor = kVersionMinor) {
    Writer w;
    for (const u8 magic : kMagic) {
        w.writeU8(magic);
    }
    w.writeU16(kVersionMajor);
    w.writeU16(minor);
    w.writeU32(flags);
    w.writeU32(static_cast<u32>(defs.size()));
    w.writeU32(0); // reserved

    u32 offset = kHeaderSize + static_cast<u32>(defs.size()) * 12;
    for (const auto& [id, size] : defs) {
        w.writeU32(static_cast<u32>(id));
        w.writeU32(offset);
        w.writeU32(static_cast<u32>(size));
        offset += static_cast<u32>(size);
    }
    for (const auto& [id, size] : defs) {
        (void)id;
        for (std::size_t i = 0; i < size; ++i) {
            w.writeU8(0xAB);
        }
    }
    return w.data();
}

// A raw header + table with caller-controlled fields; no payload bytes.
std::vector<u8> rawLayout(u16 major, u16 minor, u32 flags, u32 count, u32 reserved,
                          std::initializer_list<std::array<u32, 3>> entries) {
    Writer w;
    for (const u8 magic : kMagic) {
        w.writeU8(magic);
    }
    w.writeU16(major);
    w.writeU16(minor);
    w.writeU32(flags);
    w.writeU32(count);
    w.writeU32(reserved);
    for (const auto& [id, offset, size] : entries) {
        w.writeU32(id);
        w.writeU32(offset);
        w.writeU32(size);
    }
    return w.data();
}

void appendZeros(std::vector<u8>& bytes, std::size_t n) {
    bytes.insert(bytes.end(), n, 0);
}

void expectError(const std::vector<u8>& bytes, std::size_t at) {
    bool threw = false;
    try {
        (void)parseLayout(bytes);
    } catch (const FormatError& error) {
        threw = true;
        CHECK(error.offset == at);
    }
    REQUIRE(threw);
}

} // namespace

TEST_CASE("valid layout parses and round-trips") {
    const auto bytes = makeLayout(
        {{SectionId::Meta, 16}, {SectionId::Const, 8}, {SectionId::Code, 32}});
    const auto layout = parseLayout(bytes);

    REQUIRE(layout.header.versionMajor == 0);
    REQUIRE(layout.header.versionMinor == 1);
    REQUIRE(layout.header.flags == 0);
    REQUIRE_FALSE(layout.header.hasDebug());
    REQUIRE(layout.totalSize == bytes.size());
    REQUIRE(layout.sections.size() == 3);

    // Payloads start after header (20) + table (3 * 12) = 56.
    REQUIRE(layout.sections[0].id == SectionId::Meta);
    REQUIRE(layout.sections[0].offset == 56);
    REQUIRE(layout.sections[0].size == 16);
    REQUIRE(layout.sections[1].id == SectionId::Const);
    REQUIRE(layout.sections[1].offset == 72);
    REQUIRE(layout.sections[1].size == 8);
    REQUIRE(layout.sections[2].id == SectionId::Code);
    REQUIRE(layout.sections[2].offset == 80);
    REQUIRE(layout.sections[2].size == 32);

    REQUIRE(layout.find(SectionId::Meta) == &layout.sections[0]);
    REQUIRE(layout.find(SectionId::Code) == &layout.sections[2]);
    REQUIRE(layout.find(SectionId::Debug) == nullptr);
}

TEST_CASE("debug section with matching flag parses") {
    const auto bytes = makeLayout({{SectionId::Meta, 4}, {SectionId::Const, 4},
                                   {SectionId::Code, 4}, {SectionId::Debug, 8}},
                                  kFlagDebug);
    const auto layout = parseLayout(bytes);
    REQUIRE(layout.header.hasDebug());
    REQUIRE(layout.find(SectionId::Debug) != nullptr);
    REQUIRE(layout.find(SectionId::Debug)->size == 8);
}

TEST_CASE("sections in any order are accepted") {
    const auto bytes = makeLayout({{SectionId::Code, 4}, {SectionId::Debug, 4},
                                   {SectionId::Meta, 4}, {SectionId::Const, 4}},
                                  kFlagDebug);
    const auto layout = parseLayout(bytes);
    REQUIRE(layout.sections.size() == 4);
    REQUIRE(layout.find(SectionId::Meta) != nullptr);
}

TEST_CASE("bad magic fails at the mismatching byte") {
    for (std::size_t corrupt = 0; corrupt < 4; ++corrupt) {
        auto bytes = makeLayout({{SectionId::Meta, 0}, {SectionId::Const, 0}, {SectionId::Code, 0}});
        bytes[corrupt] ^= 0xFF;
        expectError(bytes, corrupt);
    }
}

TEST_CASE("truncated header fails") {
    expectError({}, 0);
    expectError({0x49, 0x50}, 2);
}

TEST_CASE("unsupported versions fail at their field offsets") {
    expectError(rawLayout(1, 1, 0, 1, 0, {{1, 0, 0}}), 4); // major 1
    expectError(rawLayout(0, 2, 0, 1, 0, {{1, 0, 0}}), 6); // minor 2
}

TEST_CASE("unknown flag bits fail") {
    expectError(rawLayout(0, 1, 2, 1, 0, {{1, 0, 0}}), 8);
}

TEST_CASE("section count out of range fails") {
    expectError(rawLayout(0, 1, 0, 0, 0, {}), 12);
    expectError(rawLayout(0, 1, 0, 256, 0, {}), 12);
}

TEST_CASE("non-zero reserved field fails") {
    expectError(rawLayout(0, 1, 0, 1, 7, {{1, 0, 0}}), 16);
}

TEST_CASE("truncated section table fails") {
    // Header declares 3 sections but the file ends right after the header.
    expectError(rawLayout(0, 1, 0, 3, 0, {}), 20);
}

TEST_CASE("unknown section id fails at the entry") {
    const auto bytes = rawLayout(0, 1, 0, 3, 0, {{1, 56, 0}, {2, 56, 0}, {99, 56, 0}});
    expectError(bytes, 20 + 2 * 12);
}

TEST_CASE("duplicate section id fails at the second entry") {
    const auto bytes = rawLayout(0, 1, 0, 3, 0, {{1, 56, 0}, {2, 56, 0}, {2, 56, 0}});
    expectError(bytes, 20 + 2 * 12);
}

TEST_CASE("section outside file bounds fails") {
    auto bytes = rawLayout(0, 1, 0, 3, 0, {{1, 60, 4}, {2, 64, 4}, {3, 999, 4}});
    appendZeros(bytes, 100 - bytes.size());
    expectError(bytes, 20 + 2 * 12);
}

TEST_CASE("overlapping sections fail at the second entry") {
    auto bytes = rawLayout(0, 1, 0, 3, 0, {{1, 60, 10}, {2, 64, 10}, {3, 80, 10}});
    appendZeros(bytes, 90 - bytes.size());
    expectError(bytes, 20 + 1 * 12);
}

TEST_CASE("missing required sections fail") {
    expectError(rawLayout(0, 1, 0, 2, 0, {{1, 44, 0}, {2, 44, 0}}), 0);
    expectError(rawLayout(0, 1, 0, 1, 0, {{3, 32, 0}}), 0);
}

TEST_CASE("debug flag/section mismatch fails") {
    // Flag set but no debug section.
    expectError(rawLayout(0, 1, kFlagDebug, 3, 0, {{1, 56, 0}, {2, 56, 0}, {3, 56, 0}}), 0);
    // Debug section present but flag clear.
    expectError(rawLayout(0, 1, 0, 4, 0, {{1, 68, 0}, {2, 68, 0}, {3, 68, 0}, {4, 68, 0}}), 0);
}
