#include "iprop.h"

#include "BuildVersion.hpp"
#include "Sized.hpp"
#include "iprop/Constants.hpp"

#include <cstddef>

extern "C" iprop_status_ex iprop_library_query(iprop_library_info_ex* out) {
    uint32_t size = 0;
    if (!iprop::capi::descriptorSize(out, size)) return IPROP_EX_E_ARGUMENT;
    iprop_library_info_ex info{};
    info.product_major = iprop::capi::productMajor;
    info.product_minor = iprop::capi::productMinor;
    info.product_patch = iprop::capi::productPatch;
    info.api_major = IPROP_API_VERSION_MAJOR;
    info.api_minor = IPROP_API_VERSION_MINOR;
    info.abi_major = iprop::capi::abiMajor;
    info.abi_minor = iprop::capi::abiMinor;
    info.format_major = iprop::kVersionMajor;
    info.format_minor_min = 1;
    info.format_minor_max = iprop::kVersionMinor;
    info.capabilities = IPROP_CAP_LIBRARY_QUERY | IPROP_CAP_SIZED_DESCRIPTORS;
#define WRITE(field) iprop::capi::writeField(out, size, offsetof(iprop_library_info_ex, field), info.field)
    WRITE(product_major); WRITE(product_minor); WRITE(product_patch);
    WRITE(api_major); WRITE(api_minor); WRITE(abi_major); WRITE(abi_minor);
    WRITE(format_major); WRITE(format_minor_min); WRITE(format_minor_max);
    WRITE(capabilities);
#undef WRITE
    return IPROP_EX_OK;
}
