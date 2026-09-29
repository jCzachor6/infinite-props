#pragma once

#include <cstdint>
#include <span>
#include <string>

#include "iprop/Types.hpp"

namespace iprop {

// Disassembles a .iprop file into .ipa text — the reference assembler
// format — with mnemonics and operands resolved to names (functions, parts,
// output slots, constants). Feeding the result to `ipa` reassembles the
// same instruction streams. Throws FormatError for files that do not parse
// or validate.
std::string disassemble(std::span<const u8> bytes);

// Like disassemble, but annotates the output with the debug section's
// source map: local names and `.ips` line numbers appear as `;` comments
// (the assembler ignores them, so the output still reassembles).
// Files without a debug section disassemble unchanged.
std::string disassembleWithSource(std::span<const u8> bytes);

// Entry point of the ipdis CLI: `ipdis file.iprop`.
int ipdisMain(int argc, char* argv[]);

} // namespace iprop
