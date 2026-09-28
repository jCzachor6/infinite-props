#pragma once

// Shared fixture builders for interpreter tests. Keeps every test file
// focused on assertions instead of re-implementing the binary layout.

#include "iprop/Consts.hpp"
#include "iprop/Format.hpp"
#include "iprop/Meta.hpp"

#include <array>
#include <cstddef>
#include <initializer_list>
#include <string>
#include <utility>
#include <vector>

namespace test_support {

using iprop::Color;
using iprop::SectionId;
using iprop::f32;
using iprop::i32;
using iprop::u16;
using iprop::u32;
using iprop::u64;
using iprop::u8;

// A valid file: header + table + payload bytes (payload sizes may be zero).
std::vector<u8> makeLayout(const std::vector<std::pair<SectionId, std::size_t>>& defs,
                           u32 flags = 0, u16 minor = iprop::kVersionMinor);

// A raw header + table with caller-controlled fields; no payload bytes.
std::vector<u8> rawLayout(u16 major, u16 minor, u32 flags, u32 count, u32 reserved,
                          std::initializer_list<std::array<u32, 3>> entries);

void appendZeros(std::vector<u8>& bytes, std::size_t n);

// `const` section bytes (spec §5).
std::vector<u8> constSection(const std::vector<std::string>& strings,
                             const std::vector<i32>& ints,
                             const std::vector<f32>& floats,
                             const std::vector<Color>& colors);

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

// One §6.1 function entry (type lists) + its raw instruction bytes.
struct FuncSpec {
    u32 nameIndex;
    std::vector<u8> paramTypes;
    std::vector<u8> resultTypes;
    std::vector<u8> localTypes;
    std::vector<u8> instructions;
};

// One §6.3 part entry.
struct PartSpec {
    u32 funcIndex;
    u32 sizeX, sizeY;
    f32 ox, oy;
    std::vector<InputSpec> inputs;
    std::vector<OutputSpec> outputs;
};

// One 4-byte instruction: opcode, aux, operand (little-endian).
std::vector<u8> ins(u8 opcode, u8 aux, u16 operand);

// A single `return` instruction.
std::vector<u8> retIns();

// The canonical draw function spec: (i32 variant, i32 archetype), named
// "draw" (name index 0), no results.
FuncSpec drawSpec(const std::vector<u8>& body, u32 nameIndex = 0,
                  const std::vector<u8>& params = {0, 0});

// `code` section bytes (spec §6): function table, contiguous streams, parts.
std::vector<u8> codeSection(const std::vector<FuncSpec>& funcs,
                            const std::vector<PartSpec>& parts);

// A minimal valid meta (8x8, 1 variant, 1 archetype, no I/O).
std::vector<u8> minimalMeta();

// `meta` section bytes (spec §4).
std::vector<u8> metaSection(u32 sizeX, u32 sizeY, f32 ox, f32 oy, u32 variants,
                            u32 archetypes, u64 fuel, bool emission, Color emColor,
                            f32 emX, f32 emY, f32 emR,
                            const std::vector<InputSpec>& inputs,
                            const std::vector<OutputSpec>& outputs,
                            u16 extraFlags = 0);

// A complete valid file: meta, const, code sections in id order.
std::vector<u8> fullFile(const std::vector<u8>& meta, const std::vector<u8>& pool,
                         const std::vector<u8>& code = {});

// A const pool holding only the given strings, enough for meta fixtures.
std::vector<u8> namesPool(const std::vector<std::string>& names);

// A const pool with the given names plus one entry in each value pool
// (int 0, float 0.0, opaque black) for bodies that push constants.
std::vector<u8> richPool(const std::vector<std::string>& names);

// Asserts parseLayout throws FormatError at `at`.
void expectError(const std::vector<u8>& bytes, std::size_t at);

// Asserts parseMeta throws at metaOffset + relOffset with `messagePart`.
void expectMetaError(const std::vector<u8>& metaBytes, const std::vector<u8>& poolBytes,
                     std::size_t relOffset, const char* messagePart);

// Asserts parseConsts throws at constOffset + relOffset with `messagePart`.
void expectConstError(const std::vector<u8>& poolBytes, std::size_t relOffset,
                      const char* messagePart);

} // namespace test_support
