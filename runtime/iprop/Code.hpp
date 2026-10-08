#pragma once

#include "iprop/Consts.hpp"
#include "iprop/Meta.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace iprop {

// Stack value types (spec §6.1 type codes: 0 = i32, 1 = f32, 2 = colour,
// 3 = vec2).
enum class ValueType : u8 {
    I32 = 0,
    F32 = 1,
    Color = 2,
    Vec2 = 3,
    Distribution = 4, // v0.5
};

// One function declaration plus its instruction stream (spec §6.1/§6.2).
// `code` is a view into the file buffer passed to parseCode; it stays valid
// for as long as that buffer does. `fileOffset` is the absolute offset of
// `code[0]` in that buffer, used for located validation/trap errors.
struct Function {
    u32 nameIndex = kAnonymousName;
    std::vector<ValueType> params;
    std::vector<ValueType> results;
    std::vector<ValueType> locals; // additional locals beyond params
    std::span<const u8> code;
    std::size_t fileOffset = 0;

    bool isAnonymous() const { return nameIndex == kAnonymousName; }
};

// One imported part (spec §6.3): a function plus its own I/O signature and
// canvas geometry.
struct Part {
    u32 functionIndex = 0;
    u32 sizeX = 0;
    u32 sizeY = 0;
    Vec2 origin{};
    std::vector<InputDecl> inputs;
    std::vector<OutputDecl> outputs;
    // From the v0.5 parts section (spec v0.5 §6); absent in older files.
    std::optional<u32> nameIndex;
    u32 parent = kRootParent;
};

// Parsed `code` section (spec §6).
struct CodeSection {
    std::vector<Function> functions;
    std::vector<Part> parts;

    // Function 0 is the entry point (spec §6.1).
    const Function& entry() const { return functions[0]; }
};

// Parses the `code` section (spec §6). Throws located FormatError on any
// layout violation: function count range, valid name indices and type codes,
// contiguous 4-byte-aligned instruction streams within the section, valid
// part entries with in-pool names and unique per-part signatures, no
// trailing bytes. Semantic checks (draw signature, acyclicity, opcode
// validity) belong to the Phase 6 validator.
CodeSection parseCode(std::span<const u8> data, const Section& section,
                      const ConstPool& pool);

// Parses the v0.5 `parts` section (spec v0.5 §6) into the parsed part table:
// one name and parent per part, names in the pool and unique, every parent a
// preceding part or kRootParent, no trailing bytes.
void parsePartInstances(std::span<const u8> data, const Section& section,
                        const ConstPool& pool, CodeSection& code);

} // namespace iprop
