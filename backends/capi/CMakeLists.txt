# The C API (task 6): the stable C surface over the runtime — a shared
# library plus a static twin, both named `ipropc`. Every native backend
# (SFML, Godot) links this one library; the C++ runtime headers are not
# part of the ABI (see README.md in this directory).

add_library(iprop_capi SHARED
    iprop.cpp)

add_library(iprop::capi ALIAS iprop_capi)
target_link_libraries(iprop_capi PRIVATE iprop::runtime)
target_compile_definitions(iprop_capi PRIVATE IPROP_CAPI_BUILD)
target_include_directories(iprop_capi PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
iprop_warnings(iprop_capi)

set_target_properties(iprop_capi PROPERTIES
    OUTPUT_NAME ipropc
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    EXPORT_NAME capi)

add_library(iprop_capi_static STATIC
    iprop.cpp)

target_link_libraries(iprop_capi_static PUBLIC iprop::runtime)
add_library(iprop::capi_static ALIAS iprop_capi_static)
target_compile_definitions(iprop_capi_static PUBLIC IPROP_CAPI_STATIC)
target_include_directories(iprop_capi_static PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
iprop_warnings(iprop_capi_static)

set_target_properties(iprop_capi_static PROPERTIES
    OUTPUT_NAME ipropc
    POSITION_INDEPENDENT_CODE ON
    EXPORT_NAME capi_static)
if(WIN32)
    # MSVC's static archive and the DLL import library both use .lib.
    set_target_properties(iprop_capi_static PROPERTIES OUTPUT_NAME ipropc_static)
endif()

# The conformance (task 6.2): the golden corpus, assembled to .iprop at
# build time by the ipa tool, then driven through the C entry point by a
# C-only executable and compared against the committed golden PNGs.
if(IPROP_BUILD_TESTS)
set(IPROP_CAPI_GOLDEN_DIR ${CMAKE_BINARY_DIR}/capi-golden)
file(MAKE_DIRECTORY ${IPROP_CAPI_GOLDEN_DIR})

set(IPROP_CAPI_SOURCES
    ${CMAKE_SOURCE_DIR}/examples/portrait.ipa
    ${CMAKE_SOURCE_DIR}/tests/golden/shapes.ipa
    ${CMAKE_SOURCE_DIR}/tests/golden/noise.ipa
    ${CMAKE_SOURCE_DIR}/tests/golden/parts.ipa
    ${CMAKE_CURRENT_SOURCE_DIR}/test/uniforms.ipa
    ${CMAKE_CURRENT_SOURCE_DIR}/test/vector-branch.ipa)

set(IPROP_CAPI_IPROPS "")
foreach(source IN LISTS IPROP_CAPI_SOURCES)
    get_filename_component(name ${source} NAME_WE)
    set(out ${IPROP_CAPI_GOLDEN_DIR}/${name}.iprop)
    add_custom_command(
        OUTPUT ${out}
        COMMAND $<TARGET_FILE:ipa> ${source} -o ${out}
        DEPENDS ipa ${source}
        COMMENT "Assembling ${name}.iprop for the C-API conformance")
    list(APPEND IPROP_CAPI_IPROPS ${out})
endforeach()

add_custom_target(capi_golden DEPENDS ${IPROP_CAPI_IPROPS})
set(owner_fixture ${IPROP_CAPI_GOLDEN_DIR}/output-owners.iprop)
add_custom_command(OUTPUT ${owner_fixture}
    COMMAND $<TARGET_FILE:ipc> ${CMAKE_CURRENT_SOURCE_DIR}/test/output-owners.ips -o ${owner_fixture}
    DEPENDS ipc test/output-owners.ips test/owner-first.ips test/owner-second.ips)
add_custom_target(capi_owners DEPENDS ${owner_fixture})
add_dependencies(capi_golden capi_owners)

foreach(source test/language.ips ../../examples/plant.ips)
    get_filename_component(name ${source} NAME_WE)
    set(out ${IPROP_CAPI_GOLDEN_DIR}/${name}.iprop)
    file(GLOB source_parts CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/examples/*.ips)
    add_custom_command(OUTPUT ${out}
        COMMAND $<TARGET_FILE:ipc> ${CMAKE_CURRENT_SOURCE_DIR}/${source} -o ${out}
        DEPENDS ipc ${source} ${source_parts})
    add_custom_target(capi_${name} DEPENDS ${out})
    add_dependencies(capi_golden capi_${name})
endforeach()

add_executable(capi_conformance
    test/conformance.c
    test/golden.c
    test/errors.c
    test/generation.c
    test/test_support.c)
add_library(capi_png_decoder STATIC test/stb_image.c)
target_include_directories(capi_png_decoder SYSTEM PRIVATE
    ${CMAKE_SOURCE_DIR}/third_party/stb)

target_link_libraries(capi_conformance PRIVATE iprop_capi_static capi_png_decoder)
target_include_directories(capi_conformance SYSTEM PRIVATE
    ${CMAKE_SOURCE_DIR}/third_party/stb)
iprop_warnings(capi_conformance)
add_dependencies(capi_conformance capi_golden)

    add_test(NAME capi_conformance
        COMMAND capi_conformance
                ${IPROP_CAPI_GOLDEN_DIR}
                ${CMAKE_SOURCE_DIR}/tests/golden)
endif()
