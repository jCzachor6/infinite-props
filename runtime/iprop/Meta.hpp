#pragma once

#include "iprop/Binary.hpp"
#include "iprop/Consts.hpp"

#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace iprop {

// One root uniform declaration (spec §4.1). The default value occupies the
// field matching the declared type.
struct InputDecl {
    InputType type = InputType::Color;
    u32 nameIndex = 0;
    Color colorDefault{};
    f32 f32Default = 0.f;
    u32 u32Default = 0;
};

// One root output-slot declaration (spec §4.2).
struct OutputDecl {
    OutputKind kind = OutputKind::Point;
    bool isList = false;
    u32 nameIndex = 0;
};

// Shared entry readers for the §4.1/§4.2 entry shapes; used by parseMeta and
// by the part table parser in parseCode. Throw located FormatError on
// unknown types/kinds, bad list flags, non-zero reserved bytes, or name
// indices outside the const pool. Callers check name uniqueness.
InputDecl readInputEntry(Reader& reader, const ConstPool& pool);
OutputDecl readOutputEntry(Reader& reader, const ConstPool& pool);

// Default emissive metadata (spec §4).
struct Emission {
    Color color{};
    Vec2 offset{};
    f32 radius = 0.f;
};

// Parsed `meta` section (spec §4). Name indices are validated against the
// const pool at parse time and stay as indices; resolution helpers compare
// by resolved string.
//
// Slot numbering (spec §4.3): root outputs occupy global slot indices
// 0..outputs.size()-1 in declaration order; part slots follow in part order
// once the code section is parsed (Phase 5).
struct AssetMeta {
    u32 sizeX = 0;
    u32 sizeY = 0;
    Vec2 origin{};
    u64 fuel = 0;
    bool hasEmission = false;
    Emission emission{};
    std::vector<InputDecl> inputs;
    std::vector<OutputDecl> outputs;

    const InputDecl* findInput(std::string_view name, const ConstPool& pool) const;
    const OutputDecl* findOutput(std::string_view name, const ConstPool& pool) const;
};

// Parses the `meta` section (spec §4), validating name indices against
// `pool`. Throws located FormatError on any violation (positive sizes,
// fuel >= 1, reserved bytes zero, known types/kinds, unique
// names, no trailing bytes).
AssetMeta parseMeta(std::span<const u8> data, const Section& section,
                    const ConstPool& pool);

} // namespace iprop
