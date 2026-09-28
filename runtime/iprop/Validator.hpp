#pragma once

#include "iprop/Code.hpp"
#include "iprop/Consts.hpp"
#include "iprop/Format.hpp"
#include "iprop/Meta.hpp"

#include <span>

namespace iprop {

// A fully parsed file. Function code spans and strings reference the byte
// buffer passed to loadFile; it must outlive the LoadedFile and every VM
// constructed from it. Hosts keep the file bytes alive for the whole
// generation (e.g. the CLI holds the read buffer; a browser backend keeps
// the ArrayBuffer). No copy is made: this is a zero-copy loader.
struct LoadedFile {
    FileLayout layout;
    ConstPool pool;
    AssetMeta meta;
    CodeSection code;
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
