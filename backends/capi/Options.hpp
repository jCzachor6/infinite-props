#pragma once

#include "iprop.h"
#include "Sized.hpp"

namespace iprop::capi {

struct GenerateOptions {
    uint32_t seed = 0;
    uint32_t sizeX = 0, sizeY = 0;
};

enum class OptionIssue { None, NullDescriptor, DescriptorTooSmall, UnknownFlags };

// Validate the descriptor once. The VM-facing consumer gets scalar values,
// never a pointer into the caller's optional prefix. Failed validation leaves
// its output untouched; program-dependent index checks happen separately.
inline OptionIssue readGenerateOptions(const iprop_generate_options_ex* descriptor,
                                      GenerateOptions& out) noexcept {
    uint32_t size = 0;
    if (descriptor == nullptr) return OptionIssue::NullDescriptor;
    if (!descriptorSize(descriptor, size)) return OptionIssue::DescriptorTooSmall;
#define READ(field) readField<uint32_t>(descriptor, size, offsetof(iprop_generate_options_ex, field))
    if (READ(flags) != 0) return OptionIssue::UnknownFlags;
    GenerateOptions value;
    value.seed = READ(seed);
    value.sizeX = READ(size_x);
    value.sizeY = READ(size_y);
#undef READ
    out = value;
    return OptionIssue::None;
}

} // namespace iprop::capi
