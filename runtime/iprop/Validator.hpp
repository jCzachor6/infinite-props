#pragma once

#include "iprop/Code.hpp"
#include "iprop/Consts.hpp"
#include "iprop/Format.hpp"
#include "iprop/Meta.hpp"
#include "iprop/Collections.hpp"

#include <span>
#include <vector>

namespace iprop {

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

    std::span<const u8> bytes() const { return storage; }

    LoadedFile() = default;
    LoadedFile(LoadedFile&&) noexcept = default;
    LoadedFile& operator=(LoadedFile&&) noexcept = default;
    LoadedFile(const LoadedFile&) = delete;
    LoadedFile& operator=(const LoadedFile&) = delete;

private:
    friend LoadedFile loadFile(std::span<const u8> data);
    std::vector<u8> storage;
};

// Parses a complete file: layout, consts, meta, code (Phases 3-5 rules).
// Throws located FormatError on any structural violation.
LoadedFile loadFile(std::span<const u8> data);

// Static validation per spec §14, run to completion before any execution:
// entry naming and signature, per-function structured control flow and stack
// type checking (integer-only branch conditions), index ranges, reserved
// opcodes, part function signatures and part-graph acyclicity. Throws a
// located FormatError on the first violation; never mutates the file.
void validate(const LoadedFile& file);

} // namespace iprop
