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

// "IP3D", the two model sections after the 2D ones, and the vec3 type.
inline constexpr Dialect kModelDialect{{0x49, 0x50, 0x33, 0x44}, 13u, true};
inline constexpr SectionId kModelSection = static_cast<SectionId>(12);
inline constexpr SectionId kSkinsSection = static_cast<SectionId>(13);
inline constexpr u32 kModelSectionSize = 32;

// One embedded 2D program a model can generate as a texture.
struct SkinProgram {
    std::string name; // its import alias
    LoadedFile file;
    u64 hash = 0; // FNV-1a 64 of the embedded file
};

// A parsed model file. Immutable; many generations may share it.
struct LoadedModel {
    LoadedFile file; // the model program
    f32 size[3] = {0.f, 0.f, 0.f};
    f32 unitsPerMeter = 0.f;
    std::vector<SkinProgram> skins;
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

struct ModelResult {
    f32 boundsMin[3] = {0.f, 0.f, 0.f};
    f32 boundsMax[3] = {0.f, 0.f, 0.f};
    f32 size[3] = {0.f, 0.f, 0.f};
    f32 unitsPerMeter = 0.f;
    std::vector<ModelTexture> textures;
    std::vector<ModelSurface> surfaces;
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
