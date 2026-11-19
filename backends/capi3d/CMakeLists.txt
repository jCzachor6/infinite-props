# The C API of models (preview, API 0.3): the static library `iprop3dc` over
# the model runtime and the static 2D C API, whose types and helpers it
# shares. It is not installed and promises no ABI while models are a preview.

include(../../cmake/CApi3dSources.cmake)
add_library(iprop_capi3d STATIC ${IPROP_CAPI3D_IMPLEMENTATION_SOURCES})
add_library(iprop::capi3d ALIAS iprop_capi3d)

target_link_libraries(iprop_capi3d PUBLIC iprop::capi_static PRIVATE iprop::model)
target_include_directories(iprop_capi3d PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>)
iprop_warnings(iprop_capi3d)
set_target_properties(iprop_capi3d PROPERTIES
    OUTPUT_NAME iprop3dc
    POSITION_INDEPENDENT_CODE ON)

# Managed hosts load both families of functions from this one library.
include(../../cmake/CApiSources.cmake)
add_library(iprop_capi3d_shared SHARED
    ${IPROP_CAPI_IMPLEMENTATION_SOURCES} ${IPROP_CAPI3D_IMPLEMENTATION_SOURCES})
target_compile_definitions(iprop_capi3d_shared PRIVATE IPROP_CAPI_BUILD)
target_link_libraries(iprop_capi3d_shared PRIVATE iprop::model)
target_include_directories(iprop_capi3d_shared
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/../capi
    PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/../capi)
set_target_properties(iprop_capi3d_shared PROPERTIES OUTPUT_NAME iprop3dc)
iprop_warnings(iprop_capi3d_shared)
if(WIN32)
    set_target_properties(iprop_capi3d PROPERTIES OUTPUT_NAME iprop3dc_static)
endif()

if(IPROP_BUILD_TESTS)
set(fixtures ${CMAKE_BINARY_DIR}/capi3d-fixtures)
file(MAKE_DIRECTORY ${fixtures})

# Each fixture in the encodings a host meets: stripped and compressed as
# `ipc` writes by default, and plain with debug lines for located errors.
set(IPROP_CAPI3D_FIXTURES "")
function(iprop_capi3d_fixture output source)
    add_custom_command(OUTPUT ${fixtures}/${output}
        COMMAND $<TARGET_FILE:ipc> ${CMAKE_CURRENT_SOURCE_DIR}/test/${source} ${ARGN} -o ${fixtures}/${output}
        DEPENDS ipc test/${source} test/tile.ips)
    set(IPROP_CAPI3D_FIXTURES ${IPROP_CAPI3D_FIXTURES} ${fixtures}/${output} PARENT_SCOPE)
endfunction()
iprop_capi3d_fixture(crate.iprop3d crate.ips)
iprop_capi3d_fixture(crate-debug.iprop3d crate.ips --debug --no-compress)
iprop_capi3d_fixture(empty.iprop3d empty.ips)
iprop_capi3d_fixture(tile.iprop tile.ips)
# Material maps, glass and windows (M2): the corpus model and its skins.
iprop_capi3d_fixture(materials.iprop3d ../../../tests/model/corpus/materials.ips)
# 3D outputs (API 0.2, M3).
iprop_capi3d_fixture(outputs.iprop3d outputs.ips)
add_custom_target(capi3d_fixtures DEPENDS ${IPROP_CAPI3D_FIXTURES})

# iprop_capi3d_test(<name> <source>... [MODEL])
# One test executable given the fixture directory. MODEL also links the C++
# model runtime, for the tests that compare the boundary with it.
function(iprop_capi3d_test name)
    cmake_parse_arguments(PARSE_ARGV 1 test "MODEL;SHARED" "" "")
    add_executable(${name} ${test_UNPARSED_ARGUMENTS})
    if(test_SHARED)
        target_link_libraries(${name} PRIVATE iprop_capi3d_shared)
    else()
        target_link_libraries(${name} PRIVATE iprop::capi3d)
    endif()
    if(test_MODEL)
        target_link_libraries(${name} PRIVATE iprop::model)
    endif()
    target_include_directories(${name} PRIVATE ${CMAKE_SOURCE_DIR}/backends/capi/test)
    iprop_warnings(${name})
    add_dependencies(${name} capi3d_fixtures)
    add_test(NAME ${name} COMMAND ${name} ${fixtures})
endfunction()

iprop_capi3d_test(capi3d_models test/models.c)
iprop_capi3d_test(capi3d_models_shared test/models.c SHARED)
iprop_capi3d_test(capi3d_descriptors test/descriptors.c)
iprop_capi3d_test(capi3d_errors test/errors.c)
find_package(Threads REQUIRED)
iprop_capi3d_test(capi3d_threads test/threads.cpp)
target_link_libraries(capi3d_threads PRIVATE Threads::Threads)
iprop_capi3d_test(capi3d_conformance test/conformance.cpp MODEL)
endif()
