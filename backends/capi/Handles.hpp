#pragma once

#include "iprop.h"
#include "iprop/VM.hpp"

#include <string>
#include <vector>

// Private definitions shared by C adapters and future reflection/parameters.
// The public header exposes only these global opaque struct tags.
struct iprop_program {
    iprop::LoadedFile file;
};

struct iprop_result {
    iprop::Canvas image;
    std::vector<std::string> names;
    std::vector<float> packed;
    std::vector<iprop_output> outputs;
    uint64_t fuelBudget = 0;
    uint64_t fuelRemaining = 0;
};
