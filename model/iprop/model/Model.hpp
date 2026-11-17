#pragma once

// The model runtime (spec models v1.2): loads a .iprop3d file and generates
// triangles and textures from it. It extends the 2D VM with the model page
// and runs every texture as a standard 2D generation of an embedded .iprop
// program, so a 2D host library carries none of this.

#include "iprop/VM.hpp"
#include "iprop/Validator.hpp"
#include "iprop/model/Geometry.hpp"
#include "iprop/model/Ops.hpp"

#include <map>
#include <span>
#include <string>
#include <vector>

namespace iprop::model {

// "IP3D", the model sections after the 2D ones, and the vec3 type. Sections
// 14 and 15 (outputs and parts, M3) are optional.
inline constexpr Dialect kModelDialect{{0x49, 0x50, 0x33, 0x44}, 15u, true};
inline constexpr SectionId kModelSection = static_cast<SectionId>(12);
inline constexpr SectionId kSkinsSection = static_cast<SectionId>(13);
inline constexpr SectionId kOutputsSection = static_cast<SectionId>(14);
inline constexpr SectionId kPartsSection = static_cast<SectionId>(15);
inline constexpr u32 kModelSectionSize = 32;

// One embedded 2D program a model can generate as a texture.
struct SkinProgram {
    std::string name; // its import alias
    LoadedFile file;
    u64 hash = 0; // FNV-1a 64 of the embedded file
    // The first entry with the same bytes: imports of one file under
    // several names (a model and its parts) are one program.
    u32 identity = 0;
};

// A 3D output slot (M3): what the game reads back beside the mesh.
enum class ModelOutputKind : u8 { Point3d = 0, Box3d = 1 };
inline constexpr u16 kRootOwner = 0xFFFFu;
struct ModelOutputDecl {
    std::string name;
    ModelOutputKind kind = ModelOutputKind::Point3d;
    bool list = false;
    u16 owner = kRootOwner; // the part that writes it, or the model itself
};

// A 3D part (M3): an imported render asset, its functions a contiguous run
// of the function table whose first is its entry.
inline constexpr u32 kRootParent = 0xFFFFFFFFu;
struct ModelPartDecl {
    std::string name; // the import path from the model, such as "leg"
    u32 firstFunction = 0, functionCount = 0;
    u32 parent = kRootParent;
};

// A parsed model file. Immutable; many generations may share it.
struct LoadedModel {
    LoadedFile file; // the model program
    f32 size[3] = {0.f, 0.f, 0.f};
    f32 unitsPerMeter = 0.f;
    std::vector<SkinProgram> skins;
    std::vector<ModelOutputDecl> outputs;
    std::vector<ModelPartDecl> parts;
};

// Parses a complete model file and its embedded programs. Throws a located
// FormatError on any structural violation, also for a .iprop file.
LoadedModel loadModel(std::span<const u8> bytes);

// Static validation of the model program and of every embedded program;
// run it before generating. Throws a located FormatError.
void validateModel(const LoadedModel& model);

// The section payloads a writer (the assembler, the compiler) appends.
void writeModelSection(Writer& out, const f32 size[3], f32 unitsPerMeter);
struct SkinEntry {
    u32 nameIndex = 0;
    std::span<const u8> bytes; // a complete expanded .iprop file
};
void writeSkinsSection(Writer& out, std::span<const SkinEntry> entries);
struct OutputEntry {
    u32 nameIndex = 0;
    ModelOutputKind kind = ModelOutputKind::Point3d;
    bool list = false;
    u16 owner = kRootOwner;
};
void writeOutputsSection(Writer& out, std::span<const OutputEntry> entries);
struct PartEntry {
    u32 nameIndex = 0, firstFunction = 0, functionCount = 0, parent = kRootParent;
};
void writePartsSection(Writer& out, std::span<const PartEntry> entries);

u64 fnv1a64(std::span<const u8> bytes, u64 state = 0xcbf29ce484222325ull);

// ---- generation -------------------------------------------------------------

struct ModelInputs {
    u32 seed = 0;
    std::map<std::string, UniformValue> uniforms; // root inputs by name
    ExecutionLimits limits{};
};

// Blend (v1.2, M2) mixes the texture over what is behind it by its alpha.
enum class SkinAlpha : u8 { Opaque = 0, Cutout = 1, Blend = 2 };

struct ModelTexture {
    u64 key = 0;
    std::string name;                     // the program's alias
    u32 assetWidth = 0, assetHeight = 0;  // the program's declared size
    Canvas color;                         // RGBA8, rows from the top
    std::vector<AuxImage> images;         // what the program published
    bool repeats = false;                 // a triangle uses it with the repeat mapping
};

// The triangles of one host material, in emission order.
struct ModelSurface {
    u32 texture = 0;
    f32 roughness = 0.f, metallic = 0.f, emission = 0.f, cutoff = 0.f;
    SkinAlpha alpha = SkinAlpha::Opaque;
    bool twoSided = false, crisp = false;
    std::vector<f32> positions; // 3 per vertex, model units
    std::vector<f32> normals;   // 3
    std::vector<f32> tangents;  // 4: xyz and the bitangent sign
    std::vector<f32> uvs;       // 2, (0, 0) the upper left of the image
    std::vector<u32> indices;   // 3 per triangle, counterclockwise from outside
};

// An oriented box: its centre, its size along its own axes and those axes,
// unit vectors forming a rotation, in model units.
struct ModelBox {
    f32 center[3] = {0.f, 0.f, 0.f};
    f32 size[3] = {0.f, 0.f, 0.f};
    f32 axes[3][3] = {{1.f, 0.f, 0.f}, {0.f, 1.f, 0.f}, {0.f, 0.f, 1.f}}; // x, y and z axis
};

// A root output with the values written to it: none for a singular output
// never written, one for a written one, any number for a list.
struct ModelOutput {
    std::string name;
    ModelOutputKind kind = ModelOutputKind::Point3d;
    bool list = false;
    std::vector<Vec3> points;
    std::vector<ModelBox> boxes;
};

struct ModelResult {
    f32 boundsMin[3] = {0.f, 0.f, 0.f};
    f32 boundsMax[3] = {0.f, 0.f, 0.f};
    f32 size[3] = {0.f, 0.f, 0.f};
    f32 unitsPerMeter = 0.f;
    std::vector<ModelTexture> textures;
    std::vector<ModelSurface> surfaces;
    std::vector<ModelOutput> outputs; // the model's own, in declaration order
    u64 fuelBudget = 0, fuelUsed = 0;
    u64 workBudget = 0, workUsed = 0;
    u32 vertexCount = 0, triangleCount = 0;
};

// Generates one model. The caller MUST have run validateModel() first.
// Throws Trap (a FormatError) on a runtime violation, in the model program
// or in a texture; nothing is published then.
ModelResult generateModel(const LoadedModel& model, const ModelInputs& inputs);
// Root inputs already resolved: one value per declaration, in order, each of
// its declared binary type. `inputs.uniforms` must be empty.
ModelResult generateModel(const LoadedModel& model, const ModelInputs& inputs,
                          std::span<const UniformValue> rootValues);

} // namespace iprop::model
