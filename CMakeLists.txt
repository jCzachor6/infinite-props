cmake_minimum_required(VERSION 3.24)

project(iprop
    VERSION 0.1.0
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

option(IPROP_BUILD_TESTS "Build the test suite" ON)
option(IPROP_FETCH_DEPS "Fetch test dependencies from the network (Catch2)" ON)

if(IPROP_BUILD_TESTS)
    enable_testing()
endif()

# Directory layout follows README.md §14: each component is a top-level
# directory (runtime/, tools/, language/, compiler/, backends/, ...).
add_subdirectory(runtime)
add_subdirectory(language)
add_subdirectory(compiler)
add_subdirectory(tools)
add_subdirectory(backends/capi)

if(IPROP_BUILD_TESTS)
    add_subdirectory(tests)
endif()
