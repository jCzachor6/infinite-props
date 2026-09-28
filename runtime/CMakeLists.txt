add_library(iprop_runtime
    version.cpp
    format.cpp
    consts.cpp
    meta.cpp
    code.cpp
    validator.cpp
    vm.cpp
    utf8.cpp)

add_library(iprop::runtime ALIAS iprop_runtime)

# Public headers live in iprop/ so consumers include "iprop/Version.hpp".
target_include_directories(iprop_runtime PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

iprop_warnings(iprop_runtime)
