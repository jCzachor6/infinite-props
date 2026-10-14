cmake_minimum_required(VERSION 3.24)
if(NOT DEFINED IPROP_SOURCE OR NOT DEFINED WORK)
    message(FATAL_ERROR "Pass IPROP_SOURCE and an isolated WORK directory")
endif()
include("${IPROP_SOURCE}/cmake/CAbi.cmake")
file(MAKE_DIRECTORY "${WORK}")

# Exercise CMake's actual find_package resolver against future additive
# products using the production version-file generator. The build's product
# version is untouched; these packages contain only version-selection fixtures.
function(check product request expected)
    string(MAKE_C_IDENTIFIER "${product}_${request}" identifier)
    set(prefix "${WORK}/package-${product}")
    file(MAKE_DIRECTORY "${prefix}")
    iprop_write_package_version("${prefix}" "${product}")
    file(WRITE "${prefix}/ipropConfig.cmake"
        "set(iprop_C_ABI_SOVERSION \"${IPROP_C_ABI_SOVERSION}\")\n")
    set(source "${WORK}/source-${identifier}")
    file(MAKE_DIRECTORY "${source}")
    file(WRITE "${source}/CMakeLists.txt"
        "cmake_minimum_required(VERSION 3.24)\n"
        "project(package_selection LANGUAGES NONE)\n"
        "find_package(iprop ${request} QUIET CONFIG PATHS \"${prefix}\" NO_DEFAULT_PATH)\n"
        "if(iprop_FOUND)\n"
        "  if(NOT ${expected} OR NOT iprop_VERSION VERSION_EQUAL \"${product}\" OR NOT iprop_C_ABI_SOVERSION STREQUAL \"0.5\")\n"
        "    message(FATAL_ERROR \"Unexpected accepted package/version/ABI\")\n"
        "  endif()\n"
        "elseif(${expected})\n"
        "  message(FATAL_ERROR \"Compatible additive product rejected\")\n"
        "endif()\n")
    execute_process(COMMAND "${CMAKE_COMMAND}" -S "${source}" -B "${WORK}/build-${identifier}"
        RESULT_VARIABLE status OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr)
    if(NOT status STREQUAL "0")
        message(FATAL_ERROR "product ${product}, request ${request}:\n${stdout}\n${stderr}")
    endif()
endfunction()

check(0.7.0 "0.7" TRUE)
check(0.7.0 "0.6" FALSE)
check(0.7.0 "0.4" FALSE)
check(0.7.0 "0.7.0 EXACT" TRUE)
check(0.7.0 "0.7.1" FALSE)
check(0.8.0 "0.7" TRUE)
check(0.8.0 "0.8" TRUE)
check(0.8.0 "0.6...0.8" FALSE)
check(0.8.0 "0.7...0.8" TRUE)
check(0.8.0 "0.7...<0.8" FALSE)
message(STATUS "C API package compatibility floor 0.7.0 on ABI line 0.5: PASS")
