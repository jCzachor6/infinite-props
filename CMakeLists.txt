cmake_minimum_required(VERSION 3.24)

project(iprop
    VERSION 0.5.0
    DESCRIPTION "A portable binary format and toolchain for seed-based procedural 2D assets"
    LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# The C API and its conformance are C (the stable surface for C hosts).
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

include(cmake/CompilerWarnings.cmake)
include(GNUInstallDirs)

# The front end type-checks helpers depth first, one recursion level per call
# in a chain, and a chain as long as the 256-frame call limit nearly fills
# Windows' 1 MiB default stack with MSVC. Executables built here (tools,
# tests, hosts) reserve the 8 MiB that Linux and macOS give a main thread.
if(MSVC)
    string(APPEND CMAKE_EXE_LINKER_FLAGS " /STACK:8388608")
elseif(MINGW)
    string(APPEND CMAKE_EXE_LINKER_FLAGS " -Wl,--stack,8388608")
endif()
include(cmake/CAbi.cmake)

option(IPROP_BUILD_TESTS "Build the test suite" ON)
option(IPROP_FETCH_DEPS "Fetch test dependencies from the network (Catch2)" ON)
foreach(backend SFML GODOT)
    set(IPROP_${backend} AUTO CACHE STRING "Build ${backend}: AUTO, ON (required), OFF")
    set_property(CACHE IPROP_${backend} PROPERTY STRINGS AUTO ON OFF)
    if(NOT IPROP_${backend} MATCHES "^(AUTO|ON|OFF)$")
        message(FATAL_ERROR "IPROP_${backend} must be AUTO, ON or OFF")
    endif()
endforeach()

if(IPROP_BUILD_TESTS)
    enable_testing()
endif()

# Directory layout follows README.md §14: each component is a top-level
# directory (runtime/, tools/, language/, compiler/, backends/, ...).
add_subdirectory(runtime)
add_subdirectory(language)
add_subdirectory(compiler)
add_subdirectory(tools)
include(cmake/LanguageDocs.cmake)
add_subdirectory(benchmarks)
add_subdirectory(backends/capi)
add_subdirectory(backends/sfml)
add_subdirectory(backends/godot)

if(IPROP_BUILD_TESTS)
    add_subdirectory(tests)
endif()

include(cmake/Install.cmake)
