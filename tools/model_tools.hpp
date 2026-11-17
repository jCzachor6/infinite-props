#pragma once

// What the CLI tools do with a generated model (spec models v1.2): a binary
// glTF file for any 3D viewer, a preview image from a small software
// rasterizer, and a dump with exact float bits for conformance checks.
// The preview is a convenience for people and review sheets; only the model
// data is a contract.

#include "png.hpp"

#include "iprop/model/Model.hpp"

#include <ostream>
#include <span>
#include <string>
#include <vector>

namespace iprop::tools {

// Whether these bytes are a model file (also inside a zlib envelope).
bool isModelFile(std::span<const u8> bytes);

// A binary glTF 2.0 file: one mesh, a primitive per surface, PNG textures
// embedded, the node scaled to metres.
std::vector<u8> encodeGlb(const model::ModelResult& result);

// A neutral backdrop: review images look the same in every viewer.
inline constexpr Color kPreviewBackground{0x2c, 0x30, 0x38, 0xff};

struct PreviewOptions {
    u32 width = 256, height = 256;
    f32 yaw = 35.f, pitch = 20.f; // where the camera stands, in degrees
    Color background = kPreviewBackground;
};
// "YAW,PITCH" in degrees: any yaw, a pitch within -89..89.
bool parseView(const std::string& text, PreviewOptions& options);
// The model seen from a fixed orbit, framed by its nominal size and its
// actual bounds, lit from the upper left with its normal maps applied.
Image renderPreview(const model::ModelResult& result, const PreviewOptions& options = {});

// One JSON document with every number that the contract fixes: floats as
// their binary32 bits, images as FNV-1a 64 hashes of their RGBA bytes.
void writeModelDump(std::ostream& out, const model::ModelResult& result, u32 seed);

} // namespace iprop::tools
