#pragma once

#include <string>
#include <vector>

#include "iprop/Types.hpp"

namespace iprop {

// Assembles .ipa text into .iprop bytes (spec §2–§6). Throws
// std::runtime_error with a line number on syntax or resolution errors.
// The assembled output is validated with the runtime validator before it is
// returned (dogfooding), so a malformed program is reported here.
std::vector<u8> assemble(const std::string& source);

// Entry point of the ipa CLI: `ipa in.ipa -o out.iprop`.
int ipaMain(int argc, char* argv[]);

} // namespace iprop
