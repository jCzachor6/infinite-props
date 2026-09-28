#include "test_support.hpp"

#include <catch2/catch_test_macros.hpp>

#include "iprop/Binary.hpp"

#include <string>

namespace test_support {

std::vector<u8> makeLayout(const std::vector<std::pair<SectionId, std::size_t>>& defs,
                           u32 flags, u16 minor) {
    iprop::Writer w;
    for (const u8 magic : iprop::kMagic) {
        w.writeU8(magic);
    }
    w.writeU16(iprop::kVersionMajor);
    w.writeU16(minor);
    w.writeU32(flags);
    w.writeU32(static_cast<u32>(defs.size()));
    w.writeU32(0); // reserved

    u32 offset = iprop::kHeaderSize + static_cast<u32>(defs.size()) * 12;
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

std::vector<u8> rawLayout(u16 major, u16 minor, u32 flags, u32 count, u32 reserved,
                          std::initializer_list<std::array<u32, 3>> entries) {
    iprop::Writer w;
    for (const u8 magic : iprop::kMagic) {
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

std::vector<u8> constSection(const std::vector<std::string>& strings,
                             const std::vector<i32>& ints,
                             const std::vector<f32>& floats,
                             const std::vector<Color>& colors) {
    iprop::Writer w;
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

std::vector<u8> metaSection(u32 sizeX, u32 sizeY, f32 ox, f32 oy, u32 variants,
                            u32 archetypes, u64 fuel, bool emission, Color emColor,
                            f32 emX, f32 emY, f32 emR,
                            const std::vector<InputSpec>& inputs,
                            const std::vector<OutputSpec>& outputs,
                            u16 extraFlags) {
    iprop::Writer w;
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

std::vector<u8> fullFile(const std::vector<u8>& meta, const std::vector<u8>& pool,
                         const std::vector<u8>& code) {
    iprop::Writer w;
    for (const u8 m : iprop::kMagic) {
        w.writeU8(m);
    }
    w.writeU16(iprop::kVersionMajor);
    w.writeU16(iprop::kVersionMinor);
    w.writeU32(0);
    w.writeU32(3);
    w.writeU32(0);
    u32 off = iprop::kHeaderSize + 3 * 12;
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

std::vector<u8> namesPool(const std::vector<std::string>& names) {
    return constSection(names, {}, {}, {});
}

void expectError(const std::vector<u8>& bytes, std::size_t at) {
    bool threw = false;
    try {
        (void)iprop::parseLayout(bytes);
    } catch (const iprop::FormatError& error) {
        threw = true;
        CHECK(error.offset == at);
    }
    REQUIRE(threw);
}

void expectMetaError(const std::vector<u8>& metaBytes, const std::vector<u8>& poolBytes,
                     std::size_t relOffset, const char* messagePart) {
    const auto bytes = fullFile(metaBytes, poolBytes);
    const auto layout = iprop::parseLayout(bytes);
    const auto pool = iprop::parseConsts(bytes, *layout.find(SectionId::Const));
    const std::size_t base = layout.find(SectionId::Meta)->offset;
    bool threw = false;
    try {
        (void)iprop::parseMeta(bytes, *layout.find(SectionId::Meta), pool);
    } catch (const iprop::FormatError& error) {
        threw = true;
        CHECK(error.offset == base + relOffset);
        CHECK(std::string(error.what()).find(messagePart) != std::string::npos);
    }
    REQUIRE(threw);
}

void expectConstError(const std::vector<u8>& poolBytes, std::size_t relOffset,
                      const char* messagePart) {
    const auto bytes = fullFile({}, poolBytes);
    const auto layout = iprop::parseLayout(bytes);
    const std::size_t base = layout.find(SectionId::Const)->offset;
    bool threw = false;
    try {
        (void)iprop::parseConsts(bytes, *layout.find(SectionId::Const));
    } catch (const iprop::FormatError& error) {
        threw = true;
        CHECK(error.offset == base + relOffset);
        CHECK(std::string(error.what()).find(messagePart) != std::string::npos);
    }
    REQUIRE(threw);
}

} // namespace test_support
