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

// Entry point of the ipdis CLI: `ipdis file.iprop`.
int ipdisMain(int argc, char* argv[]);

} // namespace iprop
