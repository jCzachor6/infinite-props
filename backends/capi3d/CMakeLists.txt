# The C API of models (preview, API 0.1): the static library `iprop3dc` over
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
add_custom_target(capi3d_fixtures DEPENDS ${IPROP_CAPI3D_FIXTURES})

# iprop_capi3d_test(<name> <source>... [MODEL])
# One test executable given the fixture directory. MODEL also links the C++
# model runtime, for the tests that compare the boundary with it.
function(iprop_capi3d_test name)
    cmake_parse_arguments(PARSE_ARGV 1 test "MODEL" "" "")
    add_executable(${name} ${test_UNPARSED_ARGUMENTS})
    target_link_libraries(${name} PRIVATE iprop::capi3d)
    if(test_MODEL)
        target_link_libraries(${name} PRIVATE iprop::model)
    endif()
    target_include_directories(${name} PRIVATE ${CMAKE_SOURCE_DIR}/backends/capi/test)
    iprop_warnings(${name})
    add_dependencies(${name} capi3d_fixtures)
    add_test(NAME ${name} COMMAND ${name} ${fixtures})
endfunction()

iprop_capi3d_test(capi3d_models test/models.c)
iprop_capi3d_test(capi3d_descriptors test/descriptors.c)
iprop_capi3d_test(capi3d_errors test/errors.c)
iprop_capi3d_test(capi3d_conformance test/conformance.cpp MODEL)
endif()
