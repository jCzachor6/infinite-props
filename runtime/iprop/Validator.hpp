#pragma once

#include "iprop/Code.hpp"
#include "iprop/Consts.hpp"
#include "iprop/Format.hpp"
#include "iprop/Instruction.hpp"
#include "iprop/Meta.hpp"
#include "iprop/Collections.hpp"
#include "iprop/NormalMap.hpp"

#include <span>
#include <string>
#include <vector>

namespace iprop {

// Record flags (formerly a reserved zero field). A normal output is a layer
// slot whose value must be a direction layer; the root's is published.
// Material maps (v1.2) are layer slots of colour layers, bits 1-4; a record
// sets one image bit at most, and bit k publishes AuxImageKind k + 1.
inline constexpr u16 kResourceExportNormal = 1u << 0;
inline constexpr u16 kResourceExportRoughness = 1u << 1;
inline constexpr u16 kResourceExportMetallic = 1u << 2;
inline constexpr u16 kResourceExportAo = 1u << 3;
inline constexpr u16 kResourceExportEmission = 1u << 4;
inline constexpr u16 kResourceExportImages = 0x1fu;
struct ResourceExport {
    u32 owner = kRootParent;
    u32 nameIndex = 0;
    TypeDescriptor type;
    bool normal = false;
    // The image the root's slot publishes: 0 for none, else an AuxImageKind.
    u32 image = 0;
};
// The record flag of an image kind (1-5), and the kind of a flag.
constexpr u16 resourceExportFlag(u32 kind) { return static_cast<u16>(1u << (kind - 1)); }

// A fully parsed file. The LoadedFile OWNS its byte buffer: every span
// (function code, strings) points into the internal storage, so the file is
// self-contained and may be moved around freely. Move-only (a copy would
// invalidate the spans); hosts keep the LoadedFile alive for the whole
// generation.
struct LoadedFile {
    Header containerHeader; // physical encoding; layout describes the expanded file
    FileLayout layout;
    ConstPool pool;
    AssetMeta meta;
    CodeSection code;
    CollectionPool collections;
    std::vector<ResourceExport> resourceExports;
    // Present with resource exports, in function-table order. Explicit ownership
    // also validates helper functions and instructions in unreachable code.
    std::vector<u32> functionOwners;

    std::span<const u8> bytes() const { return storage; }

    LoadedFile() = default;
    LoadedFile(LoadedFile&&) noexcept = default;
    LoadedFile& operator=(LoadedFile&&) noexcept = default;
    LoadedFile(const LoadedFile&) = delete;
    LoadedFile& operator=(const LoadedFile&) = delete;

private:
    friend LoadedFile loadFile(std::span<const u8> data, const Dialect& dialect);
    std::vector<u8> storage;
};

// Parses a complete file: layout, consts, meta, code (Phases 3-5 rules).
// Throws located FormatError on any structural violation. A dialect's own
// sections (above SectionId::Steppers) stay in the layout for its reader.
LoadedFile loadFile(std::span<const u8> data, const Dialect& dialect = kPropDialect);
bool hasPartOutputs(const LoadedFile& file) noexcept;
void parseResourceExports(std::span<const u8> data, const Section& section, LoadedFile& file);
void validateResourceOwnership(const LoadedFile& file);

// The typed stack and labels of the function being validated, as an
// instruction check of an extended instruction set may use them. They behave
// exactly as the built-in checks do: nothing is checked in unreachable code.
class InstructionCheck {
public:
    virtual void expect(ValueType type, const char* what) = 0;
    virtual void push(ValueType type) = 0;
    // Opens a scope label that the matching `end` closes; scopes of every
    // kind share the limit of 32.
    virtual void openScope() = 0;
    [[noreturn]] virtual void fail(std::string message) = 0;
    virtual const LoadedFile& file() const = 0;

protected:
    ~InstructionCheck() = default;
};

// An extended instruction set validated by another library (the model VM,
// spec models v1.2). A 2D file is validated without one.
class ValidationExtension {
public:
    // The required name of function 0.
    virtual const char* entryName() const = 0;
    // Whether a 2D instruction belongs to the extended set.
    virtual bool admits(Opcode opcode) const = 0;
    // Checks an instruction whose opcode the 2D set reserves. Returns false
    // when the extended set does not define the opcode either.
    virtual bool check(Instruction instruction, InstructionCheck& state) const = 0;

protected:
    ~ValidationExtension() = default;
};

// Static validation per spec §14, run to completion before any execution:
// entry naming and signature, per-function structured control flow and stack
// type checking (integer-only branch conditions), index ranges, reserved
// opcodes, part function signatures and part-graph acyclicity. Throws a
// located FormatError on the first violation; never mutates the file.
void validate(const LoadedFile& file, const ValidationExtension* extension = nullptr);

} // namespace iprop
