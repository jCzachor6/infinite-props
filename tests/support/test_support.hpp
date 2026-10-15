#pragma once

// Shared fixture builders for interpreter tests. Keeps every test file
// focused on assertions instead of re-implementing the binary layout.

#include "iprop/Canvas.hpp"
#include "iprop/Consts.hpp"
#include "iprop/Format.hpp"
#include "iprop/Meta.hpp"
#include "iprop/Value.hpp"

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
                           u32 flags = 0);

// A raw header + table with caller-controlled fields; no payload bytes.
// `former` fills the reserved bytes that once held the format version.
std::vector<u8> rawLayout(u32 former, u32 flags, u32 count, u32 reserved,
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

// The canonical draw function spec: declared uniforms only, named
// "draw" (name index 0), no results.
FuncSpec drawSpec(const std::vector<u8>& body, u32 nameIndex = 0,
                  const std::vector<u8>& params = {});

// A tiny program builder for VM tests: deduplicates constants into the
// const pool and emits instructions. Programs leave values on the entry
// stack and are executed WITHOUT validation; the VM's runtime checks still
// apply and its debugStack() exposes the result.
struct Prog {
    std::vector<u8> body;
    std::vector<i32> ints;
    std::vector<f32> floats;
    std::vector<Color> colors;

    Prog& emit(u8 opcode, u16 operand = 0);
    Prog& I(i32 value);  // const.i32 with pool dedup
    Prog& F(f32 value);  // const.f32 with pool dedup
    Prog& C(Color value); // const.color with pool dedup
};

// A complete single-draw file for a Prog (meta size/fuel configurable, pool
// from the prog's constants).
std::vector<u8> buildProgFile(const Prog& prog, u32 sizeX = 8, u32 sizeY = 8);

// Runs without validation and returns the entry frame's final stack.
std::vector<iprop::Value> evalProg(const Prog& prog);
i32 evalProgI32(const Prog& prog);
f32 evalProgF32(const Prog& prog);
Color evalProgColor(const Prog& prog);

// Runs without validation and returns the canvas.
iprop::Canvas runProgCanvas(const Prog& prog, u32 sizeX = 8, u32 sizeY = 8);

// Expects run() to trap with a message mentioning `messagePart`.
void expectProgTrap(const Prog& prog, const char* messagePart);

// Bit-pattern helpers for exact f32 comparisons.
u32 bitsOf(f32 value);
f32 f32FromBits(u32 value);

// `code` section bytes (spec §6): function table, contiguous streams, parts.
std::vector<u8> codeSection(const std::vector<FuncSpec>& funcs,
                            const std::vector<PartSpec>& parts);

// A minimal valid meta (8x8, no I/O).
std::vector<u8> minimalMeta();

// `meta` section bytes (spec §4).
std::vector<u8> metaSection(u32 sizeX, u32 sizeY, f32 ox, f32 oy, u64 fuel, bool emission, Color emColor,
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
