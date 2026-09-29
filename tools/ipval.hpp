#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "iprop/Types.hpp"

namespace iprop {

// One located validation violation (spec §14: hosts reject invalid files
// with a defined error carrying a byte offset).
struct Violation {
    std::size_t offset = 0;
    std::string message;
};

// The result of validating one file. The runtime validator is fail-fast:
// the report carries the first violation it finds.
struct ValidationReport {
    bool valid = false;
    std::vector<Violation> violations;
};

// Loads and validates a .iprop file, reporting the first violation with its
// byte offset. Never throws: structural problems become violations.
ValidationReport validateReport(std::span<const u8> bytes);

// Entry point of the ipval CLI: `ipval [--json] file.iprop`.
// Exit codes: 0 = valid, 1 = invalid, 2 = usage or I/O error.
int ipvalMain(int argc, char* argv[]);

} // namespace iprop
