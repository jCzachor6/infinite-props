#pragma once

#include "iprop/VM.hpp"

#include <string>
#include <vector>

namespace compiler_test {

struct RunOutcome {
    iprop::Canvas canvas;
    std::vector<iprop::NamedOutput> outputs;
};

std::vector<iprop::u8> compileChecked(const std::string& source);
RunOutcome execute(const std::vector<iprop::u8>& bytes, iprop::u32 seed = 42);
RunOutcome run(const std::vector<iprop::u8>& bytes, iprop::u32 variant, iprop::u32 archetype);

} // namespace compiler_test
