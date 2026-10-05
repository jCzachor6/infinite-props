# The loader identity is a binary compatibility contract, not a release number.
# An ABI break must change this line and the earliest compatible product
# together, with an explicit migration. Additive product/API releases retain it.
set(IPROP_C_ABI_VERSION_MAJOR 0)
set(IPROP_C_ABI_VERSION_MINOR 4)
set(IPROP_C_ABI_SOVERSION "${IPROP_C_ABI_VERSION_MAJOR}.${IPROP_C_ABI_VERSION_MINOR}")
set(IPROP_C_ABI_MIN_PRODUCT_VERSION "0.4.0")

function(iprop_write_package_version output_dir product_version)
    include(CMakePackageConfigHelpers)
    write_basic_package_version_file("${output_dir}/ipropProductVersion.cmake"
        VERSION "${product_version}" COMPATIBILITY AnyNewerVersion)
    configure_file("${CMAKE_CURRENT_FUNCTION_LIST_DIR}/ipropConfigVersion.cmake.in"
        "${output_dir}/ipropConfigVersion.cmake" @ONLY)
endfunction()
