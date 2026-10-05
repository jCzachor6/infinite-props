#include "iprop.h"
#include "Sized.hpp"

#include <cstddef>

extern "C" {

iprop_status_ex iprop_library_info_ex_init(iprop_library_info_ex* out, uint32_t capacity) {
    if (out == nullptr || capacity < sizeof(uint32_t)) return IPROP_EX_E_ARGUMENT;
    const uint32_t zero = 0;
#define ZERO(field) iprop::capi::writeField(out, capacity, offsetof(iprop_library_info_ex, field), zero)
    ZERO(product_major); ZERO(product_minor); ZERO(product_patch);
    ZERO(api_major); ZERO(api_minor); ZERO(abi_major); ZERO(abi_minor);
    ZERO(format_major); ZERO(format_minor_min); ZERO(format_minor_max);
#undef ZERO
    const uint64_t noCapabilities = 0;
    iprop::capi::writeField(out, capacity, offsetof(iprop_library_info_ex, capabilities), noCapabilities);
    iprop::capi::writeField(out, capacity, 0, capacity);
    return IPROP_EX_OK;
}

iprop_status_ex iprop_generate_options_ex_init(iprop_generate_options_ex* out, uint32_t capacity) {
    if (out == nullptr || capacity < sizeof(uint32_t)) return IPROP_EX_E_ARGUMENT;
    const uint32_t zero = 0;
#define ZERO(field) iprop::capi::writeField(out, capacity, offsetof(iprop_generate_options_ex, field), zero)
    ZERO(flags); ZERO(seed); ZERO(variant); ZERO(archetype); ZERO(size_x); ZERO(size_y);
#undef ZERO
    iprop::capi::writeField(out, capacity, 0, capacity);
    return IPROP_EX_OK;
}

} // extern "C"
