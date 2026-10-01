# The HTML5/WASM backend: the reference interpreter compiled to WebAssembly
# with a small embind surface (generate(fileBytes, inputs) -> {data, ...}).
#
# Build (from the repository root):
#   emcmake cmake -S backends/html5 -B build/wasm -DCMAKE_BUILD_TYPE=Release
#   cmake --build build/wasm --parallel 4
# Outputs: iprop.js, iprop.wasm in build/wasm.

cmake_minimum_required(VERSION 3.24)
project(iprop_html5 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(../../cmake/CompilerWarnings.cmake)

include(../../cmake/RuntimeSources.cmake)
add_library(iprop_runtime STATIC ${IPROP_RUNTIME_SOURCES})

target_include_directories(iprop_runtime PUBLIC ../../runtime)

# Spec §13.2: no fused multiply-add — the WASM build must disable the
# contraction like every other backend, or its pixels diverge from the
# native reference.
iprop_warnings(iprop_runtime)

# Runtime traps must reach bindings.cpp's FormatError catch and become JS
# Errors. Emscripten otherwise disables C++ catching and aborts the module.
# PUBLIC also enables catching in the binding translation unit.
target_compile_options(iprop_runtime PUBLIC -fexceptions)

add_executable(iprop_html5
    bindings.cpp)

target_link_libraries(iprop_html5 PRIVATE iprop_runtime embind)

set_target_properties(iprop_html5 PROPERTIES
    OUTPUT_NAME "iprop"
    LINK_FLAGS "-sMODULARIZE=1 -sEXPORT_NAME=createIpropModule -sENVIRONMENT=web,node")

target_link_options(iprop_html5 PRIVATE
    "-fexceptions"
    "-sMODULARIZE=1"
    "-sEXPORT_NAME=createIpropModule"
    "-sENVIRONMENT=web,node"
    "-sALLOW_MEMORY_GROWTH=1"
    "-sNO_EXIT_RUNTIME=1")
