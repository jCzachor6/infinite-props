#pragma once

#include "iprop/Constants.hpp"
#include "iprop/Format.hpp"
#include "iprop/Types.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace iprop {

// Parsed `const` section (spec §5). Indices are 0-based per pool; the i32
// pool also serves as the u32 constant pool.
struct ConstPool {
    std::vector<std::string> strings;
    std::vector<i32> ints;
    std::vector<f32> floats;
    std::vector<Color> colors;
};

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
    u32 variants = 0;
    u32 archetypes = 0;
    u64 fuel = 0;
    bool hasEmission = false;
    Emission emission{};
    std::vector<InputDecl> inputs;
    std::vector<OutputDecl> outputs;

    const InputDecl* findInput(std::string_view name, const ConstPool& pool) const;
    const OutputDecl* findOutput(std::string_view name, const ConstPool& pool) const;
};

// Parses the `const` section (spec §5). Throws located FormatError on any
// violation (spec §14: string lengths within section, UTF-8 well-formedness).
ConstPool parseConsts(std::span<const u8> data, const Section& section);

// Parses the `meta` section (spec §4), validating name indices against
// `pool`. Throws located FormatError on any violation (positive sizes,
// counts >= 1, fuel >= 1, known types/kinds, unique names).
AssetMeta parseMeta(std::span<const u8> data, const Section& section,
                    const ConstPool& pool);

} // namespace iprop
