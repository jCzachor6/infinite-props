# The C API (task 6): the stable C surface over the runtime — a shared
# library plus a static twin, both named `ipropc`. Every native backend
# (SFML, Godot) links this one library; the C++ runtime headers are not
# part of the ABI (see README.md in this directory).

configure_file(BuildVersion.hpp.in ${CMAKE_CURRENT_BINARY_DIR}/BuildVersion.hpp @ONLY)
set(iprop_capi_sources iprop.cpp operations.cpp library.cpp descriptors.cpp reflection.cpp)
add_library(iprop_capi SHARED ${iprop_capi_sources})

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
    SOVERSION ${IPROP_C_ABI_SOVERSION}
    EXPORT_NAME capi)

add_library(iprop_capi_static STATIC ${iprop_capi_sources})

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

foreach(target iprop_capi iprop_capi_static)
    target_include_directories(${target} PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
endforeach()

# The conformance (task 6.2): the golden corpus, assembled to .iprop at
# build time by the ipa tool, then driven through the C entry point by a
# C-only executable and compared against the committed golden PNGs.
if(IPROP_BUILD_TESTS)
add_test(NAME capi_package_compatibility COMMAND ${CMAKE_COMMAND}
    "-DIPROP_SOURCE=${CMAKE_SOURCE_DIR}"
    "-DWORK=${CMAKE_BINARY_DIR}/capi-package-compatibility"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/test/package_compatibility.cmake")
add_executable(capi_descriptors test/descriptors.cpp)
target_link_libraries(capi_descriptors PRIVATE iprop_capi_static)
iprop_warnings(capi_descriptors)
add_test(NAME capi_descriptors COMMAND capi_descriptors)
foreach(linkage shared static)
    add_executable(capi_error_descriptor_${linkage} test/error_descriptor.c)
    add_executable(capi_error_buffers_${linkage} test/error_buffers.c)
    add_executable(capi_checked_${linkage} test/checked.c)
    add_executable(capi_reflection_${linkage} test/reflection.c)
    add_executable(capi_output_declarations_${linkage} test/output_declarations.c)
    add_executable(capi_parts_${linkage} test/parts.c)
    add_executable(capi_metadata_${linkage} test/metadata.c)
    add_executable(capi_program_info_old_${linkage} test/program_info_old_client.c)
    add_executable(capi_library_${linkage} test/library.c)
    if(linkage STREQUAL "shared")
        target_link_libraries(capi_error_descriptor_${linkage} PRIVATE iprop_capi)
        target_link_libraries(capi_error_buffers_${linkage} PRIVATE iprop_capi)
        target_link_libraries(capi_checked_${linkage} PRIVATE iprop_capi)
        target_link_libraries(capi_reflection_${linkage} PRIVATE iprop_capi)
        target_link_libraries(capi_output_declarations_${linkage} PRIVATE iprop_capi)
        target_link_libraries(capi_parts_${linkage} PRIVATE iprop_capi)
        target_link_libraries(capi_metadata_${linkage} PRIVATE iprop_capi)
        target_link_libraries(capi_program_info_old_${linkage} PRIVATE iprop_capi)
        target_link_libraries(capi_library_${linkage} PRIVATE iprop_capi)
    else()
        target_link_libraries(capi_error_descriptor_${linkage} PRIVATE iprop_capi_static)
        target_link_libraries(capi_error_buffers_${linkage} PRIVATE iprop_capi_static)
        target_link_libraries(capi_checked_${linkage} PRIVATE iprop_capi_static)
        target_link_libraries(capi_reflection_${linkage} PRIVATE iprop_capi_static)
        target_link_libraries(capi_output_declarations_${linkage} PRIVATE iprop_capi_static)
        target_link_libraries(capi_parts_${linkage} PRIVATE iprop_capi_static)
        target_link_libraries(capi_metadata_${linkage} PRIVATE iprop_capi_static)
        target_link_libraries(capi_program_info_old_${linkage} PRIVATE iprop_capi_static)
        target_link_libraries(capi_library_${linkage} PRIVATE iprop_capi_static)
    endif()
    target_compile_definitions(capi_library_${linkage} PRIVATE
        IPROP_TEST_PRODUCT_MAJOR=${PROJECT_VERSION_MAJOR}
        IPROP_TEST_PRODUCT_MINOR=${PROJECT_VERSION_MINOR}
        IPROP_TEST_PRODUCT_PATCH=${PROJECT_VERSION_PATCH})
    iprop_warnings(capi_library_${linkage})
    iprop_warnings(capi_error_descriptor_${linkage})
    iprop_warnings(capi_error_buffers_${linkage})
    iprop_warnings(capi_checked_${linkage})
    iprop_warnings(capi_reflection_${linkage})
    iprop_warnings(capi_output_declarations_${linkage})
    iprop_warnings(capi_parts_${linkage})
    iprop_warnings(capi_metadata_${linkage})
    iprop_warnings(capi_program_info_old_${linkage})
    add_test(NAME capi_error_descriptor_${linkage} COMMAND capi_error_descriptor_${linkage})
    add_test(NAME capi_library_${linkage} COMMAND capi_library_${linkage})
endforeach()
set(IPROP_CAPI_GOLDEN_DIR ${CMAKE_BINARY_DIR}/capi-golden)
file(MAKE_DIRECTORY ${IPROP_CAPI_GOLDEN_DIR})

set(IPROP_CAPI_SOURCES
    ${CMAKE_SOURCE_DIR}/examples/portrait.ipa
    ${CMAKE_SOURCE_DIR}/tests/golden/shapes.ipa
    ${CMAKE_SOURCE_DIR}/tests/golden/noise.ipa
    ${CMAKE_SOURCE_DIR}/tests/golden/parts.ipa
    ${CMAKE_CURRENT_SOURCE_DIR}/test/uniforms.ipa
    ${CMAKE_CURRENT_SOURCE_DIR}/test/error-trap.ipa
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
foreach(linkage shared static)
    add_dependencies(capi_error_buffers_${linkage} capi_golden)
    add_dependencies(capi_checked_${linkage} capi_golden)
    add_test(NAME capi_error_buffers_${linkage} COMMAND capi_error_buffers_${linkage}
        ${IPROP_CAPI_GOLDEN_DIR}/uniforms.iprop)
    add_test(NAME capi_checked_${linkage} COMMAND capi_checked_${linkage}
        ${IPROP_CAPI_GOLDEN_DIR}/uniforms.iprop)
endforeach()
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

foreach(name reflection-defaults reflection-empty)
    set(out ${IPROP_CAPI_GOLDEN_DIR}/${name}.iprop)
    add_custom_command(OUTPUT ${out}
        COMMAND $<TARGET_FILE:ipc> ${CMAKE_CURRENT_SOURCE_DIR}/test/${name}.ips --no-compress -o ${out}
        DEPENDS ipc test/${name}.ips)
    add_custom_target(capi_${name} DEPENDS ${out})
    add_dependencies(capi_golden capi_${name})
endforeach()
foreach(linkage shared static)
    add_dependencies(capi_reflection_${linkage} capi_golden)
    add_test(NAME capi_reflection_${linkage} COMMAND capi_reflection_${linkage}
        ${IPROP_CAPI_GOLDEN_DIR}/reflection-defaults.iprop
        ${IPROP_CAPI_GOLDEN_DIR}/reflection-empty.iprop)
endforeach()

foreach(encoding plain debug compressed)
    set(out ${IPROP_CAPI_GOLDEN_DIR}/reflection-outputs-${encoding}.iprop)
    if(encoding STREQUAL "plain")
        set(output_schema_flags --strip --no-compress)
    elseif(encoding STREQUAL "debug")
        set(output_schema_flags --debug --no-compress)
    else()
        set(output_schema_flags --debug --compress)
    endif()
    add_custom_command(OUTPUT ${out}
        COMMAND $<TARGET_FILE:ipc> ${CMAKE_CURRENT_SOURCE_DIR}/test/reflection-outputs.ips
            ${output_schema_flags} -o ${out}
        DEPENDS ipc test/reflection-outputs.ips test/reflection-output-first.ips
            test/reflection-output-empty.ips test/reflection-output-second.ips)
    add_custom_target(capi_output_schema_${encoding} DEPENDS ${out})
    add_dependencies(capi_golden capi_output_schema_${encoding})
endforeach()
foreach(linkage shared static)
    add_dependencies(capi_output_declarations_${linkage} capi_golden)
    add_test(NAME capi_output_declarations_${linkage} COMMAND capi_output_declarations_${linkage}
        ${IPROP_CAPI_GOLDEN_DIR}/reflection-outputs-plain.iprop
        ${IPROP_CAPI_GOLDEN_DIR}/reflection-outputs-debug.iprop
        ${IPROP_CAPI_GOLDEN_DIR}/reflection-outputs-compressed.iprop
        ${IPROP_CAPI_GOLDEN_DIR}/reflection-empty.iprop)
endforeach()

foreach(encoding plain debug compressed)
    set(out ${IPROP_CAPI_GOLDEN_DIR}/reflection-parts-${encoding}.iprop)
    if(encoding STREQUAL "plain")
        set(part_schema_flags --strip --no-compress)
    elseif(encoding STREQUAL "debug")
        set(part_schema_flags --debug --no-compress)
    else()
        set(part_schema_flags --debug --compress)
    endif()
    add_custom_command(OUTPUT ${out}
        COMMAND $<TARGET_FILE:ipc> ${CMAKE_CURRENT_SOURCE_DIR}/test/reflection-parts.ips
            ${part_schema_flags} -o ${out}
        DEPENDS ipc test/reflection-parts.ips test/reflection-part-first.ips
            test/reflection-part-empty.ips test/reflection-part-second.ips)
    add_custom_target(capi_part_schema_${encoding} DEPENDS ${out})
    add_dependencies(capi_golden capi_part_schema_${encoding})
endforeach()
foreach(linkage shared static)
    add_dependencies(capi_parts_${linkage} capi_golden)
    add_test(NAME capi_parts_${linkage} COMMAND capi_parts_${linkage}
        ${IPROP_CAPI_GOLDEN_DIR}/reflection-parts-plain.iprop
        ${IPROP_CAPI_GOLDEN_DIR}/reflection-parts-debug.iprop
        ${IPROP_CAPI_GOLDEN_DIR}/reflection-parts-compressed.iprop
        ${IPROP_CAPI_GOLDEN_DIR}/reflection-empty.iprop)
    add_dependencies(capi_metadata_${linkage} capi_golden)
    add_dependencies(capi_program_info_old_${linkage} capi_golden)
    add_test(NAME capi_metadata_${linkage} COMMAND capi_metadata_${linkage}
        ${IPROP_CAPI_GOLDEN_DIR}/reflection-parts-plain.iprop
        ${IPROP_CAPI_GOLDEN_DIR}/reflection-parts-debug.iprop
        ${IPROP_CAPI_GOLDEN_DIR}/reflection-parts-compressed.iprop)
    add_test(NAME capi_program_info_old_${linkage} COMMAND capi_program_info_old_${linkage}
        ${IPROP_CAPI_GOLDEN_DIR}/uniforms.iprop)
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

add_executable(capi_error_sink test/error_sink.cpp)
target_link_libraries(capi_error_sink PRIVATE iprop_capi_static)
iprop_warnings(capi_error_sink)
add_dependencies(capi_error_sink capi_golden)
add_test(NAME capi_error_sink COMMAND capi_error_sink
    ${IPROP_CAPI_GOLDEN_DIR}/uniforms.iprop ${owner_fixture})

add_executable(capi_error_boundary test/error_boundary.cpp)
target_link_libraries(capi_error_boundary PRIVATE iprop_capi_static)
iprop_warnings(capi_error_boundary)
add_dependencies(capi_error_boundary capi_golden)
add_test(NAME capi_error_boundary COMMAND capi_error_boundary
    ${IPROP_CAPI_GOLDEN_DIR}/uniforms.iprop ${IPROP_CAPI_GOLDEN_DIR}/error-trap.iprop)

add_executable(capi_checked_errors test/checked_errors.cpp)
target_link_libraries(capi_checked_errors PRIVATE iprop_capi_static)
iprop_warnings(capi_checked_errors)
add_dependencies(capi_checked_errors capi_golden)
add_test(NAME capi_checked_errors COMMAND capi_checked_errors ${IPROP_CAPI_GOLDEN_DIR}/uniforms.iprop
    ${IPROP_CAPI_GOLDEN_DIR}/reflection-outputs-plain.iprop
    ${IPROP_CAPI_GOLDEN_DIR}/reflection-parts-plain.iprop)

    add_test(NAME capi_conformance
        COMMAND capi_conformance
                ${IPROP_CAPI_GOLDEN_DIR}
                ${CMAKE_SOURCE_DIR}/tests/golden)
endif()
