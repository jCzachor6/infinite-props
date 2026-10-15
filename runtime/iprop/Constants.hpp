#pragma once

#include "iprop/Types.hpp"

namespace iprop {

// File header (spec §2.1). The format is unversioned: the four bytes after
// the magic are reserved and must be zero.
inline constexpr u8 kMagic[4] = {0x49, 0x50, 0x52, 0x50}; // "IPRP"
inline constexpr u32 kHeaderSize = 20;
inline constexpr u32 kFlagDebug = 1u << 0; // debug section present
inline constexpr u32 kFlagCompressed = 1u << 1; // v0.4 zlib envelope
inline constexpr u32 kMaxCompressedFileSize = 1u << 26; // encoded and decoded, 64 MiB

// Section ids (spec §2.2).
enum class SectionId : u32 {
    Meta = 1,
    Const = 2,
    Code = 3,
    Debug = 4,
    Parts = 5, // v0.5: part instance names and parents (spec v0.5 §6)
    Types = 6, // v0.6: parameterized type descriptors
    Collections = 7, // v0.6: immutable collection constants
    Samplers = 8, // v0.6: immutable sampler constants
    Distributions = 9, // v0.6: immutable point distribution constants
    ResourceExports = 10, // v0.6: internal mask/layer exports and function owners
    Steppers = 11, // v0.7: immutable stepper constants
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
    F32Sin = 0x4F,

    // Colour (§9.5)
    ColorMix = 0x50,
    ColorShade = 0x51,
    ColorSetA = 0x52, // v0.3: the alpha set
    F32Cos = 0x53,    // trigonometry, placed in the post-colour gap (§9.4)

    // The math package (§9.4/§12.8)
    F32Floor = 0x54,
    F32Ceil = 0x55,
    F32Min = 0x56,
    F32Max = 0x57,
    F32Pow = 0x58,
    F32Exp = 0x59,
    F32Atan2 = 0x5A,

    // Vec2 (spec §9.4; added in v0.3)
    Vec2Make = 0x5B,
    Vec2Add = 0x5C,
    Vec2Sub = 0x5D,
    Vec2Mul = 0x5E,
    Vec2Div = 0x5F,

    // Seeded primitives (§9.6)
    StreamPush = 0x60,
    StreamPop = 0x61,
    RngUnit = 0x62,
    RngRange = 0x63,
    RngIndex = 0x64,
    Hash = 0x65,
    StreamState = 0x66,

    // Noise and helpers (§9.7)
    Noise = 0x70,
    Fault = 0x71,
    Smooth = 0x72,
    Lerp = 0x73,

    // Vec2 unary ops (spec §9.4; added in v0.3, placed after the helpers)
    Vec2Neg = 0x74,
    Vec2Len = 0x75,
    Vec2Dot = 0x76,
    Vec2Rot = 0x77,
    Vec2X = 0x78,
    Vec2Y = 0x79,

    // Signed-distance fields (spec §9.4/§12.9; added in v0.3)
    DistCircle = 0x7A,
    DistRect = 0x7B,
    DistRoundRect = 0x7C,
    DistLine = 0x7D,
    DistBezier = 0x7E,
    Fill = 0x7F,
    // Canvas (§9.8)
    Pixel = 0x80,
    Rect = 0x81,
    Line = 0x82,
    Poly = 0x83,
    Oval = 0x84,
    Weather = 0x85,
    Curve = 0x86,      // quadratic stroke, added in v0.3
    CurveCubic = 0x87, // cubic stroke, added in v0.3
    DistBezierCubic = 0x88, // placed in the post-canvas gap

    // The noise v2 (spec §9.4/§12.10; added in v0.3)
    NoiseGrad = 0x89,
    NoiseFbm = 0x8A,
    NoiseCell = 0x8B,
    Warp = 0x8C,
    CellJitter = 0x8D,
    NoiseRidge = 0x8E,

    // The curves (spec §9.4/§12.11; added in v0.3)
    Gain = 0x8F,

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

    // The curves, continued (spec §9.4/§12.11; added in v0.3)
    Quintic = 0x99,

    // The colours (spec §9.5/§12.12; added in v0.3)
    ColorMul = 0x9A,
    ColorScale = 0x9B,
    ColorHsv = 0x9C,

    // The dither, the fold and the polar (spec §9.4/§12.13; added in v0.3)
    Dither = 0x9D,
    Vec2Fold = 0x9E,
    Vec2Polar = 0x9F,

    // The curve toolkit (spec §9.4/§12.14; added in v0.3)
    Smoothstep = 0xA1,
    Remap = 0xA2,
    Clamp = 0xA3,

    // Colour channel reads (spec v0.5 §2; added in v0.5)
    ColorR = 0xA4,
    ColorG = 0xA5,
    ColorB = 0xA6,
    ColorA = 0xA7,

    // Uniform random ranges (spec v0.5 §3; added in v0.5)
    RngInt = 0xA8,
    RngFloat = 0xA9,

    // Distributions (spec v0.5 §4; added in v0.5)
    DistributionGaussian = 0xAA,
    DistributionTriangular = 0xAB,
    DistributionBeta = 0xAC,
    RngIntWith = 0xAD,
    RngFloatWith = 0xAE,

    // Gradients and segments (spec v0.5 §5; added in v0.5)
    ColorMixRgba = 0xAF,
    Vec2Mix = 0xB0,

    // Exact integer bounds and stable normalization (spec v0.6 §2–3).
    I32Min = 0xB1,
    I32Max = 0xB2,
    I32Clamp = 0xB3,
    Vec2Normalize = 0xB4,
    ConstList = 0xB5,
    ListBegin = 0xB6,
    ListAppend = 0xB7,
    ListEnd = 0xB8,
    ListGet = 0xB9,
    ListLen = 0xBA,
    PolyList = 0xBB,
    SamplerValue = 0xBC,
    SamplerRange = 0xBD,
    SamplerWith = 0xBE,
    SamplerPick = 0xBF,
    SamplerPickList = 0xC0,
    ConstSampler = 0xC1,
    ConstDistribution = 0xC2,
    DistributionBegin = 0xC3,
    DistributionAppend = 0xC4,
    DistributionEnd = 0xC5,
    LayerBegin = 0xC6,
    SurfacePaint = 0xC7,
    MaskFrom = 0xC8,
    MaskUnion = 0xC9,
    MaskIntersect = 0xCA,
    MaskSubtract = 0xCB,
    MaskInvert = 0xCC,
    MaskExpand = 0xCD,
    MaskShrink = 0xCE,
    MaskOffset = 0xCF,
    SurfacePaintMask = 0xD0,
    ClipBegin = 0xD1,
    OutMask = 0xD2,
    OutLayer = 0xD3,
    PartGetMask = 0xD4,
    PartGetLayer = 0xD5,
    ListDistinct = 0xD6,
    ListSort = 0xD7,
    StepperValue = 0xD8,
    StepperRange = 0xD9,
    StepperWith = 0xDA,
    StepperPickList = 0xDB,
    ConstStepper = 0xDC,
    LayerBlur = 0xDD,
    MaskBlur = 0xDE,

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

// rng.int draws at most this many u32 values (spec v0.5 §3.1). Part of the
// format semantics: the count fixes both the stream state and the fuel use.
inline constexpr u32 kRangeDraws = 32;

// Function table: name index marking an unnamed function (spec §6.1).
inline constexpr u32 kAnonymousName = 0xFFFFFFFFu;

// Part instance section: the parent of a part drawn by the root asset.
inline constexpr u32 kRootParent = 0xFFFFFFFFu;

} // namespace iprop
