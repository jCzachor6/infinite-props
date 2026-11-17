#pragma once

// The model page (spec models v1.2): the instructions of a model program
// that a 2D program does not have. One opcode value carries them all; the
// aux byte selects the sub-operation and the u16 operand is its immediate.
// The validator, the VM, the assembler, the disassembler and the compiler
// read this one table.

#include "iprop/Constants.hpp"
#include "iprop/TypeDescriptor.hpp"

#include <array>
#include <optional>
#include <span>
#include <string_view>

namespace iprop::model {

// Reserved in the 2D instruction set; valid only in a model file.
inline constexpr Opcode kModelPage = static_cast<Opcode>(0x0F);

enum class Op : u8 {
    Vec3Make = 0x00,
    Vec3Add = 0x01,
    Vec3Sub = 0x02,
    Vec3Mul = 0x03,
    Vec3Div = 0x04,
    Vec3Scale = 0x05,
    Vec3Neg = 0x06,
    Vec3X = 0x07,
    Vec3Y = 0x08,
    Vec3Z = 0x09,
    Vec3Len = 0x0A,
    Vec3Dot = 0x0B,
    Vec3Cross = 0x0C,
    Vec3Normalize = 0x0D,
    Vec3Mix = 0x0E,

    PlaceBegin = 0x20,

    Box = 0x40,
    BoxBevel = 0x41,
    Cylinder = 0x42,
    Cone = 0x43,
    Band = 0x44,
    Sphere = 0x45,
    Lathe = 0x46,
    LatheCrease = 0x47,
    Extrude = 0x48,
    ExtrudeBevel = 0x49,
    Quad = 0x4A,
    Tri = 0x4B,

    SkinDraw = 0x70,
    SkinDensity = 0x71,
    SkinFit = 0x72,
    SkinShift = 0x73,
    SkinRoughness = 0x74,
    SkinMetallic = 0x75,
    SkinEmission = 0x76,
    SkinCutout = 0x77,
    SkinTwoSided = 0x78,
    SkinCrisp = 0x79,
};

// The bits of place.begin's operand, in the stack order of their arguments.
inline constexpr u16 kPlaceAt = 1u << 0;
inline constexpr u16 kPlaceYaw = 1u << 1;
inline constexpr u16 kPlacePitch = 1u << 2;
inline constexpr u16 kPlaceRoll = 1u << 3;
inline constexpr u16 kPlaceScale = 1u << 4;
inline constexpr u16 kPlaceScale3 = 1u << 5;
inline constexpr u16 kPlaceMirrorX = 1u << 6;
inline constexpr u16 kPlaceMirrorY = 1u << 7;
inline constexpr u16 kPlaceMirrorZ = 1u << 8;
inline constexpr u16 kPlaceAll = (1u << 9) - 1;
// A valid mask names at least one argument and one scale form at most.
constexpr bool validPlaceMask(u16 mask) {
    return mask != 0 && (mask & ~kPlaceAll) == 0 && (mask & (kPlaceScale | kPlaceScale3)) != (kPlaceScale | kPlaceScale3);
}

// A skin on the stack is the number of its record, an i32.
inline constexpr TypeDescriptor kSkinSlot = TypeDescriptor::I32;
// The record skin.draw makes, before any modifier: the texture repeats at one
// texel to a unit, is opaque, one-sided and filtered.
inline constexpr f32 kSkinDensity = 1.f, kSkinRoughness = 0.8f, kSkinMetallic = 0.f, kSkinEmission = 0.f,
                     kSkinCutoff = 0.5f;

struct OpSpec {
    Op op;
    const char* mnemonic;
    // Deepest first, like a function signature. place.begin and skin.draw
    // take what their operand names, so their signature here is empty.
    std::array<TypeDescriptor, 6> inputs{};
    std::size_t inputCount = 0;
    std::optional<TypeDescriptor> result;
    bool operandIsImmediate = false; // otherwise the operand must be zero
};

std::span<const OpSpec> opSpecs();
const OpSpec* findOp(u8 aux);
const OpSpec* findOp(std::string_view mnemonic);

// Whether a 2D instruction belongs to the instruction set of a model
// program: everything that needs no canvas, surface, 2D output, part or 2D
// geometry handle.
bool admits(Opcode opcode);

// Limits of one generation and of one file (spec models v1.2).
inline constexpr u32 kMaxVertices = 65536;
inline constexpr u32 kMaxTriangles = 131072;
inline constexpr u32 kMaxTextures = 32;
inline constexpr u64 kMaxTexels = 4194304;
inline constexpr u32 kMaxTextureSide = 2048;
inline constexpr u32 kMaxSkinRecords = 1024;
inline constexpr u32 kMaxSkinPrograms = 64;
inline constexpr u32 kMinSides = 3, kMaxSides = 64;
inline constexpr u32 kMinRings = 2, kMaxRings = 32;
inline constexpr std::size_t kMaxProfilePoints = 64;
inline constexpr std::size_t kMaxOutlinePoints = 256;
// Arena charges, fixed across hosts.
inline constexpr u64 kVertexBytes = 48, kTriangleBytes = 16, kSkinRecordBytes = 64;

// The tariff of the model page (spec models v1.2, Accounting): what an
// instruction pays beyond its own unit. Calibrated against plain
// instructions and the raster kernels of the 2D tariff, at about the CPU
// time per unit of a plain instruction (benchmarks/model-tariff.cpp).
inline constexpr u64 kVertexFuel = 6;   // per vertex a primitive emits
inline constexpr u64 kTriangleFuel = 2; // per triangle
inline constexpr u64 kPlaceFuel = 24;   // place.begin: composing the scope
inline constexpr u64 kSkinDrawFuel = 64; // skin.draw, also when the texture exists
// The checks and the triangulation of an extrude outline of n points grow
// with n squared: ceil(n*n/3), and ceil(n*n/2) with a bevel, whose inset
// outline is checked again.
constexpr u64 outlineFuel(u64 points, bool bevelled) {
    const u64 squared = points * points;
    return bevelled ? (squared + 1) / 2 : (squared + 2) / 3;
}

} // namespace iprop::model
