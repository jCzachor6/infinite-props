include(../cmake/RuntimeSources.cmake)
add_library(iprop_runtime ${IPROP_RUNTIME_SOURCES})

add_library(iprop::runtime ALIAS iprop_runtime)
set_target_properties(iprop_runtime PROPERTIES
    POSITION_INDEPENDENT_CODE ON EXPORT_NAME runtime)

# Public headers live in iprop/ so consumers include "iprop/Version.hpp".
target_include_directories(iprop_runtime PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)

iprop_warnings(iprop_runtime)
