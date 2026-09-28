#pragma once

#include "iprop/Types.hpp"

namespace iprop {

// File header (spec §2.1).
inline constexpr u8 kMagic[4] = {0x49, 0x50, 0x52, 0x50}; // "IPRP"
inline constexpr u16 kVersionMajor = 0;
inline constexpr u16 kVersionMinor = 1;
inline constexpr u32 kHeaderSize = 20;
inline constexpr u32 kFlagDebug = 1u << 0; // debug section present

// Section ids (spec §2.2).
enum class SectionId : u32 {
    Meta = 1,
    Const = 2,
    Code = 3,
    Debug = 4,
};

// Input (uniform) types (spec §4.1).
enum class InputType : u8 {
    Color = 0,
    F32 = 1,
    U32 = 2,
};

// Output slot kinds (spec §4.2).
enum class OutputKind : u8 {
    Point = 0,
    Rect = 1,
    Circle = 2,
};

// Opcodes (spec §9). Values are part of the frozen format contract: they
// MUST NOT change. Each value below matches spec/iprop-v0.1.md §9 exactly.
enum class Opcode : u8 {
    // Control (§9.1)
    Nop = 0x00,
    Block = 0x01,
    Loop = 0x02,
    If = 0x03,
    Else = 0x04,
    End = 0x05,
    Br = 0x06,
    BrIf = 0x07,
    Call = 0x08,
    Return = 0x09,

    // Stack and constants (§9.2)
    ConstI32 = 0x10,
    ConstF32 = 0x11,
    ConstColor = 0x12,
    LocalGet = 0x13,
    LocalSet = 0x14,
    LocalTee = 0x15,
    Drop = 0x16,
    Dup = 0x17,

    // Integer (§9.3)
    I32Add = 0x20,
    I32Sub = 0x21,
    I32Mul = 0x22,
    I32DivS = 0x23,
    I32DivU = 0x24,
    I32RemS = 0x25,
    I32RemU = 0x26,
    I32And = 0x27,
    I32Or = 0x28,
    I32Xor = 0x29,
    I32Shl = 0x2A,
    I32ShrS = 0x2B,
    I32ShrU = 0x2C,
    I32Neg = 0x2D,
    I32Not = 0x2E,
    I32Eq = 0x30,
    I32Ne = 0x31,
    I32LtS = 0x32,
    I32LtU = 0x33,
    I32GtS = 0x34,
    I32GtU = 0x35,
    I32LeS = 0x36,
    I32LeU = 0x37,
    I32GeS = 0x38,
    I32GeU = 0x39,

    // Float (§9.4)
    F32Add = 0x40,
    F32Sub = 0x41,
    F32Mul = 0x42,
    F32Div = 0x43,
    F32Eq = 0x44,
    F32Ne = 0x45,
    F32Lt = 0x46,
    F32Gt = 0x47,
    F32Le = 0x48,
    F32Ge = 0x49,
    F32Neg = 0x4A,
    F32Abs = 0x4B,
    F32Sqrt = 0x4C,
    I32ToF32 = 0x4D,
    F32ToI32 = 0x4E,

    // Colour (§9.5)
    ColorMix = 0x50,
    ColorShade = 0x51,
    ColorSetA = 0x52, // reserved in v0.1; validation rejects it

    // Seeded primitives (§9.6)
    StreamPush = 0x60,
    StreamPop = 0x61,
    RngUnit = 0x62,
    RngRange = 0x63,
    RngIndex = 0x64,
    Hash = 0x65,

    // Noise and helpers (§9.7)
    Noise = 0x70,
    Fault = 0x71,
    Smooth = 0x72,
    Lerp = 0x73,

    // Canvas (§9.8)
    Pixel = 0x80,
    Rect = 0x81,
    Line = 0x82,
    Poly = 0x83,
    Oval = 0x84,
    Weather = 0x85,

    // Structured outputs (§9.9)
    OutPoint = 0x90,
    OutRect = 0x91,
    OutCircle = 0x92,
    OutListPoint = 0x93,
    OutListRect = 0x94,
    OutListCircle = 0x95,
    PartGetPoint = 0x96,
    PartGetRect = 0x97,
    PartGetCircle = 0x98,

    // Parts (§9.10)
    PartDraw = 0xA0,
};

// Validation limits (implementation-safety constants, not part of the frozen
// format semantics; MAY be relaxed without a spec change).
inline constexpr u16 kMaxFunctions = 1024;
inline constexpr u16 kMaxParts = 256;
inline constexpr u16 kMaxLocals = 1024;
inline constexpr u32 kMaxSectionSize = 1u << 26; // 64 MiB
inline constexpr u16 kMaxStackDepth = 512;
inline constexpr u16 kMaxCallDepth = 256;
inline constexpr u32 kMaxConstPoolEntries = 65536; // u16 index space
inline constexpr u32 kMaxCanvasSide = 16384;       // canvas width/height cap

// Function table: name index marking an unnamed function (spec §6.1).
inline constexpr u32 kAnonymousName = 0xFFFFFFFFu;

} // namespace iprop
