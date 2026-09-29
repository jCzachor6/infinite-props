# The HTML5/WASM backend: the reference interpreter compiled to WebAssembly
# with a small embind surface (generate(fileBytes, inputs) -> {data, ...}).
#
# Build (from the repository root):
#   mkdir -p build/wasm && cd build/wasm
#   emcmake cmake ../.. -DCMAKE_BUILD_TYPE=Release
#   emmake make -j4
# Outputs: iprop.js, iprop.wasm in this directory.

cmake_minimum_required(VERSION 3.24)
project(iprop_html5 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(../../cmake/CompilerWarnings.cmake)

add_library(iprop_runtime STATIC
    ../../runtime/version.cpp
    ../../runtime/opcode_name.cpp
    ../../runtime/debug.cpp
    ../../runtime/format.cpp
    ../../runtime/consts.cpp
    ../../runtime/meta.cpp
    ../../runtime/code.cpp
    ../../runtime/validator.cpp
    ../../runtime/vm.cpp
    ../../runtime/stack.cpp
    ../../runtime/control.cpp
    ../../runtime/arithmetic.cpp
    ../../runtime/seeded.cpp
    ../../runtime/canvas.cpp
    ../../runtime/outputs.cpp
    ../../runtime/parts.cpp
    ../../runtime/utf8.cpp)

target_include_directories(iprop_runtime PUBLIC ../../runtime)

add_executable(iprop_html5
    bindings.cpp)

target_link_libraries(iprop_html5 PRIVATE iprop_runtime embind)

set_target_properties(iprop_html5 PROPERTIES
    OUTPUT_NAME "iprop"
    LINK_FLAGS "-sMODULARIZE=1 -sEXPORT_NAME=createIpropModule -sENVIRONMENT=web,node")

target_link_options(iprop_html5 PRIVATE
    "-sMODULARIZE=1"
    "-sEXPORT_NAME=createIpropModule"
    "-sENVIRONMENT=web,node"
    "-sALLOW_MEMORY_GROWTH=1"
    "-sNO_EXIT_RUNTIME=1")
