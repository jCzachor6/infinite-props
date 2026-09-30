add_library(iprop_runtime
    version.cpp
    opcode_name.cpp
    debug.cpp
    format.cpp
    consts.cpp
    meta.cpp
    code.cpp
    validator.cpp
    vm.cpp
    stack.cpp
    control.cpp
    arithmetic.cpp
    seeded.cpp
    canvas.cpp
    outputs.cpp
    parts.cpp
    utf8.cpp)

add_library(iprop::runtime ALIAS iprop_runtime)
set_target_properties(iprop_runtime PROPERTIES
    POSITION_INDEPENDENT_CODE ON EXPORT_NAME runtime)

# Public headers live in iprop/ so consumers include "iprop/Version.hpp".
target_include_directories(iprop_runtime PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)

iprop_warnings(iprop_runtime)
