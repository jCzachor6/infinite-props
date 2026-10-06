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
    iprop::GenerationResult generation;
    std::vector<std::string> names;
    std::vector<float> packed;
    std::vector<iprop_output> outputs;
};
