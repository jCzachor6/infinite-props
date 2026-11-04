#pragma once

#include "iprop.h"
#include "iprop/VM.hpp"

#include <string>
#include <memory>
#include <vector>

// Private definitions shared by C adapters and future reflection/parameters.
// The public header exposes only these global opaque struct tags.
struct iprop_program {
    std::shared_ptr<const iprop::LoadedFile> data;
};

struct iprop_parameters {
    std::shared_ptr<const iprop::LoadedFile> data;
    std::vector<iprop::UniformValue> values;
};

namespace iprop::capi {
inline bool parametersCompatible(const iprop_program& program,
                                 const iprop_parameters& parameters) noexcept {
    return program.data && program.data.get() == parameters.data.get();
}
} // namespace iprop::capi

struct iprop_result {
    struct PartSlot {
        uint32_t outputIndex;
        std::shared_ptr<const iprop::PartOutputData> data;
    };
    iprop::Canvas image;
    std::vector<std::string> names;
    std::vector<float> packed;
    std::vector<iprop_output> outputs;
    // Only nonempty part slots need an entry (at most 256). The output table's
    // internal kind 3 is never exposed through the frozen legacy image view.
    std::vector<PartSlot> partSlots;
    uint32_t ownerCount = 1;
    uint64_t fuelBudget = 0;
    uint64_t fuelRemaining = 0;
};
static_assert(sizeof(iprop::OutputPart) + sizeof(iprop_result::PartSlot) <= 128);
