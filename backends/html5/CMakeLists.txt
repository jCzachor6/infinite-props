# The HTML5/WASM backend: the reference interpreter compiled to WebAssembly
# with a small embind surface (generate(fileBytes, inputs) -> {data, ...}).
#
# Build (from the repository root):
#   emcmake cmake -S backends/html5 -B build/wasm -DCMAKE_BUILD_TYPE=Release
#   cmake --build build/wasm --parallel 4
# Outputs: iprop.js, iprop.wasm in build/wasm.

cmake_minimum_required(VERSION 3.24)
# Keep the C API's build identity tied to the authoritative root project.
file(STRINGS ../../CMakeLists.txt iprop_version_line LIMIT_COUNT 1 REGEX "^[ \t]+VERSION [0-9]+\\.[0-9]+\\.[0-9]+")
string(REGEX REPLACE ".*VERSION ([0-9.]+).*" "\\1" iprop_version "${iprop_version_line}")
project(iprop_html5 VERSION ${iprop_version} LANGUAGES CXX)

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
target_compile_options(iprop_runtime PUBLIC -fexceptions "-sUSE_ZLIB=1")
target_link_options(iprop_runtime PUBLIC "-sUSE_ZLIB=1")

add_executable(iprop_html5
    bindings.cpp)

include(../../cmake/CAbi.cmake)
include(../../cmake/CApiSources.cmake)
configure_file(../capi/BuildVersion.hpp.in ${CMAKE_CURRENT_BINARY_DIR}/BuildVersion.hpp @ONLY)
add_library(iprop_wasm_capi STATIC ${IPROP_CAPI_IMPLEMENTATION_SOURCES})
target_include_directories(iprop_wasm_capi PUBLIC ../capi PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_compile_definitions(iprop_wasm_capi PUBLIC IPROP_CAPI_STATIC)
target_link_libraries(iprop_wasm_capi PRIVATE iprop_runtime)
iprop_warnings(iprop_wasm_capi)
target_link_libraries(iprop_html5 PRIVATE iprop_wasm_capi iprop_runtime embind)

set_target_properties(iprop_html5 PROPERTIES
    OUTPUT_NAME "iprop"
    LINK_FLAGS "-sMODULARIZE=1 -sEXPORT_NAME=createIpropModule -sENVIRONMENT=web,worker,node")

target_link_options(iprop_html5 PRIVATE
    "--pre-js" "${CMAKE_CURRENT_SOURCE_DIR}/module_api.js"
    "-fexceptions"
    "-sMODULARIZE=1"
    "-sEXPORT_NAME=createIpropModule"
    "-sENVIRONMENT=web,worker,node"
    "-sALLOW_MEMORY_GROWTH=1"
    "-sNO_EXIT_RUNTIME=1")
set_property(TARGET iprop_html5 APPEND PROPERTY LINK_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/module_api.js")

# Test executable only: validates the shared geometry representation on WASM
# before its public constructor operations land. No extra embind export.
add_executable(geometry_representation_probe ../../tests/geometry_representation_probe.cpp)
target_link_libraries(geometry_representation_probe PRIVATE iprop_runtime)
iprop_warnings(geometry_representation_probe)
target_link_options(geometry_representation_probe PRIVATE "-fexceptions" "-sENVIRONMENT=node" "-sALLOW_MEMORY_GROWTH=1")
add_executable(voronoi_probe ../../tests/voronoi_probe.cpp)
target_link_libraries(voronoi_probe PRIVATE iprop_runtime)
iprop_warnings(voronoi_probe)
target_link_options(voronoi_probe PRIVATE "-fexceptions" "-sENVIRONMENT=node" "-sALLOW_MEMORY_GROWTH=1")
add_executable(contours_probe ../../tests/contours_probe.cpp)
target_link_libraries(contours_probe PRIVATE iprop_runtime)
iprop_warnings(contours_probe)
target_link_options(contours_probe PRIVATE "-fexceptions" "-sENVIRONMENT=node" "-sALLOW_MEMORY_GROWTH=1")

add_executable(regions_probe ../../tests/regions_probe.cpp)
target_link_libraries(regions_probe PRIVATE iprop_runtime)
iprop_warnings(regions_probe)
target_link_options(regions_probe PRIVATE "-fexceptions" "-sENVIRONMENT=node" "-sALLOW_MEMORY_GROWTH=1")

add_executable(gap_bevel_probe ../../tests/gap_bevel_probe.cpp)
target_link_libraries(gap_bevel_probe PRIVATE iprop_runtime)
iprop_warnings(gap_bevel_probe)
target_link_options(gap_bevel_probe PRIVATE "-fexceptions" "-sENVIRONMENT=node" "-sALLOW_MEMORY_GROWTH=1")

add_executable(dynamic_parts_probe ../../tests/dynamic_parts_probe.cpp)
target_link_libraries(dynamic_parts_probe PRIVATE iprop_runtime)
iprop_warnings(dynamic_parts_probe)
target_link_options(dynamic_parts_probe PRIVATE "-fexceptions" "-sENVIRONMENT=node" "-sALLOW_MEMORY_GROWTH=1")

add_executable(part_exports_probe ../../tests/part_exports_probe.cpp)
target_link_libraries(part_exports_probe PRIVATE iprop_runtime)
iprop_warnings(part_exports_probe)
target_link_options(part_exports_probe PRIVATE "-fexceptions" "-sENVIRONMENT=node" "-sNODERAWFS=1" "-sALLOW_MEMORY_GROWTH=1")
