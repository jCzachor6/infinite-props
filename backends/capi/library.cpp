#include "iprop.h"

#include "BuildVersion.hpp"
#include "CheckedBoundary.hpp"
#include "iprop/Constants.hpp"

#include <cstddef>

namespace {
iprop::capi::CallOutcome queryLibrary(iprop_library_info_ex* out) noexcept {
    uint32_t size = 0;
    if (!iprop::capi::descriptorSize(out, size))
        return iprop::capi::CallOutcome::argument("library query output is null or smaller than four bytes");
    iprop_library_info_ex info{};
    info.product_major = iprop::capi::productMajor;
    info.product_minor = iprop::capi::productMinor;
    info.product_patch = iprop::capi::productPatch;
    info.api_major = IPROP_API_VERSION_MAJOR;
    info.api_minor = IPROP_API_VERSION_MINOR;
    info.abi_major = iprop::capi::abiMajor;
    info.abi_minor = iprop::capi::abiMinor;
    info.format_major = iprop::kVersionMajor;
    info.format_minor_min = iprop::kVersionMinor;
    info.format_minor_max = iprop::kVersionMinor;
    info.capabilities = IPROP_CAP_LIBRARY_QUERY | IPROP_CAP_SIZED_DESCRIPTORS |
        IPROP_CAP_PROGRAM_LOAD_EX | IPROP_CAP_PROGRAM_QUERY |
        IPROP_CAP_STRUCTURED_ERRORS | IPROP_CAP_LIBRARY_QUERY_EX |
        IPROP_CAP_ROOT_INPUT_REFLECTION | IPROP_CAP_OUTPUT_DECLARATIONS |
        IPROP_CAP_PART_REFLECTION | IPROP_CAP_METADATA_EXTENDED |
        IPROP_CAP_PARAMETERS | IPROP_CAP_RESULT_STATISTICS | IPROP_CAP_IMAGE_VIEW | IPROP_CAP_PARAMETERS_TYPED |
        IPROP_CAP_PARAMETER_OVERRIDES | IPROP_CAP_GENERATE_EX |
        IPROP_CAP_RESULT_OUTPUTS | IPROP_CAP_TYPED_OUTPUTS;
#define WRITE(field) iprop::capi::writeField(out, size, offsetof(iprop_library_info_ex, field), info.field)
    WRITE(product_major); WRITE(product_minor); WRITE(product_patch);
    WRITE(api_major); WRITE(api_minor); WRITE(abi_major); WRITE(abi_minor);
    WRITE(format_major); WRITE(format_minor_min); WRITE(format_minor_max);
    WRITE(capabilities);
#undef WRITE
    return {};
}
} // namespace

extern "C" iprop_status_ex iprop_library_query(iprop_library_info_ex* out) {
    return queryLibrary(out).succeeded ? IPROP_EX_OK : IPROP_EX_E_ARGUMENT;
}

extern "C" iprop_status_ex iprop_library_query_ex(iprop_library_info_ex* out, iprop_error_ex* error) {
    iprop::capi::ErrorContext context{iprop::ErrorStage::Arguments};
    return iprop::capi::checkedBoundary(error, out, iprop::capi::queryCapacity(out), context,
        [&] { return queryLibrary(out); });
}
