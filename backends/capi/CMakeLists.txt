# The C API (task 6): the stable C surface over the runtime — a shared
# library plus a static twin, both named `ipropc`. Every native backend
# (SFML, Godot) links this one library; the C++ runtime headers are not
# part of the ABI (see README.md in this directory).

configure_file(BuildVersion.hpp.in ${CMAKE_CURRENT_BINARY_DIR}/BuildVersion.hpp @ONLY)
set(iprop_capi_sources iprop.cpp operations.cpp library.cpp descriptors.cpp reflection.cpp results.cpp parameters.cpp parameter_setters.cpp outputs.cpp)
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

# Clients of either linkage name the library as ${capi_${linkage}}.
set(capi_shared iprop_capi)
set(capi_static iprop_capi_static)

# Real C99 host examples, built against both forms of the installed C ABI.
foreach(example parameter_panel render_inputs)
    foreach(linkage shared static)
        add_executable(${example}_${linkage} ${CMAKE_SOURCE_DIR}/examples/c/${example}.c)
        target_link_libraries(${example}_${linkage} PRIVATE ${capi_${linkage}})
        iprop_warnings(${example}_${linkage})
    endforeach()
endforeach()

# The conformance (task 6.2): the golden corpus, assembled to .iprop at
# build time by the ipa tool, then driven through the C entry point by a
# C-only executable and compared against the committed golden PNGs.
if(IPROP_BUILD_TESTS)
add_test(NAME capi_package_compatibility COMMAND ${CMAKE_COMMAND}
    "-DIPROP_SOURCE=${CMAKE_SOURCE_DIR}"
    "-DWORK=${CMAKE_BINARY_DIR}/capi-package-compatibility"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/test/package_compatibility.cmake")

set(golden ${CMAKE_BINARY_DIR}/capi-golden)
file(MAKE_DIRECTORY ${golden})

set(IPROP_CAPI_SOURCES
    ${CMAKE_SOURCE_DIR}/examples/portrait.ipa
    ${CMAKE_SOURCE_DIR}/tests/golden/shapes.ipa
    ${CMAKE_SOURCE_DIR}/tests/golden/noise.ipa
    ${CMAKE_SOURCE_DIR}/tests/golden/parts.ipa
    ${CMAKE_CURRENT_SOURCE_DIR}/test/uniforms.ipa
    ${CMAKE_CURRENT_SOURCE_DIR}/test/legacy-output-owners.ipa
    ${CMAKE_CURRENT_SOURCE_DIR}/test/error-trap.ipa
    ${CMAKE_CURRENT_SOURCE_DIR}/test/vector-branch.ipa)

set(IPROP_CAPI_IPROPS "")
foreach(source IN LISTS IPROP_CAPI_SOURCES)
    get_filename_component(name ${source} NAME_WE)
    set(out ${golden}/${name}.iprop)
    add_custom_command(
        OUTPUT ${out}
        COMMAND $<TARGET_FILE:ipa> ${source} -o ${out}
        DEPENDS ipa ${source}
        COMMENT "Assembling ${name}.iprop for the C-API conformance")
    list(APPEND IPROP_CAPI_IPROPS ${out})
endforeach()

# Every compiled fixture below is a dependency of capi_golden, which each C
# API test builds first.
add_custom_target(capi_golden DEPENDS ${IPROP_CAPI_IPROPS})

set(image_fixture ${golden}/image-view.iprop)
add_custom_command(OUTPUT ${image_fixture}
    COMMAND $<TARGET_FILE:ipc> ${CMAKE_CURRENT_SOURCE_DIR}/test/image-view.ips -o ${image_fixture}
    DEPENDS ipc test/image-view.ips)
add_custom_target(capi_image_fixture DEPENDS ${image_fixture})
add_dependencies(capi_golden capi_image_fixture)

set(owner_fixture ${golden}/output-owners.iprop)
add_custom_command(OUTPUT ${owner_fixture}
    COMMAND $<TARGET_FILE:ipc> ${CMAKE_CURRENT_SOURCE_DIR}/test/output-owners.ips -o ${owner_fixture}
    DEPENDS ipc test/output-owners.ips test/owner-first.ips test/owner-second.ips)
add_custom_target(capi_owners DEPENDS ${owner_fixture})
add_dependencies(capi_golden capi_owners)
# The separately built frozen client needs these exact two current-tool
# fixtures, assembled as format 0.4 so the released library loads them too.
add_custom_target(capi_legacy_fixtures DEPENDS
    ${golden}/uniforms.iprop ${golden}/legacy-output-owners.iprop)

set(result_output_fixture ${golden}/output-values.iprop)
add_custom_command(OUTPUT ${result_output_fixture}
    COMMAND $<TARGET_FILE:ipc> ${CMAKE_CURRENT_SOURCE_DIR}/test/output-values.ips -o ${result_output_fixture}
    DEPENDS ipc test/output-values.ips test/reflection-output-first.ips
        test/reflection-output-empty.ips test/reflection-output-second.ips)
add_custom_target(capi_result_output_fixture DEPENDS ${result_output_fixture})
add_dependencies(capi_golden capi_result_output_fixture)

foreach(source test/language.ips ../../examples/plant.ips ../../examples/language/math-bounds.ips
        ../../examples/language/asset-constants.ips ../../examples/language/collections.ips
        ../../examples/language/samplers.ips)
    get_filename_component(name ${source} NAME_WE)
    set(out ${golden}/${name}.iprop)
    file(GLOB source_parts CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/examples/*.ips)
    add_custom_command(OUTPUT ${out}
        COMMAND $<TARGET_FILE:ipc> ${CMAKE_CURRENT_SOURCE_DIR}/${source} -o ${out}
        DEPENDS ipc ${source} ${source_parts})
    add_custom_target(capi_${name} DEPENDS ${out})
    add_dependencies(capi_golden capi_${name})
endforeach()

foreach(name reflection-defaults reflection-empty)
    set(out ${golden}/${name}.iprop)
    add_custom_command(OUTPUT ${out}
        COMMAND $<TARGET_FILE:ipc> ${CMAKE_CURRENT_SOURCE_DIR}/test/${name}.ips --no-compress -o ${out}
        DEPENDS ipc test/${name}.ips)
    add_custom_target(capi_${name} DEPENDS ${out})
    add_dependencies(capi_golden capi_${name})
endforeach()

# Output and part schemas, each in three physical encodings.
set(schema_flags_plain --strip --no-compress)
set(schema_flags_debug --debug --no-compress)
set(schema_flags_compressed --debug --compress)
foreach(schema output part)
    foreach(encoding plain debug compressed)
        set(out ${golden}/reflection-${schema}s-${encoding}.iprop)
        add_custom_command(OUTPUT ${out}
            COMMAND $<TARGET_FILE:ipc> ${CMAKE_CURRENT_SOURCE_DIR}/test/reflection-${schema}s.ips
                ${schema_flags_${encoding}} -o ${out}
            DEPENDS ipc test/reflection-${schema}s.ips test/reflection-${schema}-first.ips
                test/reflection-${schema}-empty.ips test/reflection-${schema}-second.ips)
        add_custom_target(capi_${schema}_schema_${encoding} DEPENDS ${out})
        add_dependencies(capi_golden capi_${schema}_schema_${encoding})
    endforeach()
endforeach()

# iprop_capi_test(<name> <source>... [BOTH] [ARGS <arg>...])
# One test executable run with ARGS. BOTH builds a C99 client twice, as
# <name>_shared and <name>_static; otherwise <name> links the static library.
function(iprop_capi_test name)
    cmake_parse_arguments(PARSE_ARGV 1 test "BOTH" "" "ARGS")
    set(linkages static)
    if(test_BOTH)
        set(linkages shared static)
    endif()
    foreach(linkage IN LISTS linkages)
        set(target ${name})
        if(test_BOTH)
            set(target ${name}_${linkage})
        endif()
        add_executable(${target} ${test_UNPARSED_ARGUMENTS})
        target_link_libraries(${target} PRIVATE ${capi_${linkage}})
        iprop_warnings(${target})
        add_dependencies(${target} capi_golden)
        add_test(NAME ${target} COMMAND ${target} ${test_ARGS})
    endforeach()
endfunction()

iprop_capi_test(capi_descriptors test/descriptors.cpp)
iprop_capi_test(capi_error_descriptor test/error_descriptor.c BOTH)
iprop_capi_test(capi_library test/library.c BOTH)
foreach(linkage shared static)
    target_compile_definitions(capi_library_${linkage} PRIVATE
        IPROP_TEST_PRODUCT_MAJOR=${PROJECT_VERSION_MAJOR}
        IPROP_TEST_PRODUCT_MINOR=${PROJECT_VERSION_MINOR}
        IPROP_TEST_PRODUCT_PATCH=${PROJECT_VERSION_PATCH})
endforeach()
iprop_capi_test(capi_image_view test/image_view.c BOTH ARGS ${image_fixture})
iprop_capi_test(capi_error_buffers test/error_buffers.c BOTH ARGS ${golden}/uniforms.iprop)
iprop_capi_test(capi_checked test/checked.c BOTH ARGS ${golden}/uniforms.iprop)
iprop_capi_test(capi_reflection test/reflection.c BOTH
    ARGS ${golden}/reflection-defaults.iprop ${golden}/reflection-empty.iprop)
iprop_capi_test(capi_result_outputs test/result_outputs.c BOTH
    ARGS ${result_output_fixture} ${golden}/reflection-empty.iprop)
iprop_capi_test(capi_output_declarations test/output_declarations.c BOTH
    ARGS ${golden}/reflection-outputs-plain.iprop ${golden}/reflection-outputs-debug.iprop
        ${golden}/reflection-outputs-compressed.iprop ${golden}/reflection-empty.iprop)
iprop_capi_test(capi_parts test/parts.c BOTH
    ARGS ${golden}/reflection-parts-plain.iprop ${golden}/reflection-parts-debug.iprop
        ${golden}/reflection-parts-compressed.iprop ${golden}/reflection-empty.iprop ${golden}/parts.iprop)
iprop_capi_test(capi_metadata test/metadata.c BOTH
    ARGS ${golden}/reflection-parts-plain.iprop ${golden}/reflection-parts-debug.iprop
        ${golden}/reflection-parts-compressed.iprop)
iprop_capi_test(capi_stats test/stats.c BOTH
    ARGS ${golden}/reflection-outputs-plain.iprop ${golden}/reflection-empty.iprop)
iprop_capi_test(capi_parameters test/parameters.c BOTH
    ARGS ${golden}/reflection-defaults.iprop ${golden}/reflection-empty.iprop)
iprop_capi_test(capi_parameter_setters test/parameter_setters.c BOTH
    ARGS ${golden}/reflection-defaults.iprop)
iprop_capi_test(capi_parameter_overrides test/parameter_overrides.c BOTH
    ARGS ${golden}/reflection-defaults.iprop)
iprop_capi_test(capi_generate_ex test/generate_ex.c BOTH ARGS ${golden})

iprop_capi_test(capi_error_sink test/error_sink.cpp
    ARGS ${golden}/uniforms.iprop ${owner_fixture})
iprop_capi_test(capi_error_boundary test/error_boundary.cpp
    ARGS ${golden}/uniforms.iprop ${golden}/error-trap.iprop)
iprop_capi_test(capi_checked_errors test/checked_errors.cpp
    ARGS ${golden}/uniforms.iprop ${golden}/reflection-outputs-plain.iprop
        ${golden}/reflection-parts-plain.iprop)
iprop_capi_test(capi_result_storage test/result_storage.cpp)
iprop_capi_test(capi_stats_oracle test/stats_oracle.cpp
    ARGS ${golden}/reflection-outputs-plain.iprop ${golden}/reflection-empty.iprop
        ${owner_fixture} ${golden}/language.iprop ${golden}/parts.iprop)
iprop_capi_test(capi_generation_request test/generation_request.cpp ARGS ${golden}/portrait.iprop)
iprop_capi_test(capi_generate_ex_errors test/generate_ex_errors.cpp
    ARGS ${golden}/uniforms.iprop ${golden}/portrait.iprop)
iprop_capi_test(capi_parameter_transactions test/parameter_transactions.cpp
    ARGS ${golden}/reflection-defaults.iprop)
iprop_capi_test(capi_parameters_errors test/parameters_errors.cpp
    ARGS ${golden}/reflection-defaults.iprop ${golden}/reflection-empty.iprop)

foreach(linkage shared static)
    add_dependencies(parameter_panel_${linkage} capi_golden)
    add_dependencies(render_inputs_${linkage} capi_golden)
    if(Python3_Interpreter_FOUND)
        add_test(NAME capi_parameter_panel_${linkage}
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/scripts/check_parameter_panel.py
                $<TARGET_FILE:parameter_panel_${linkage}>
                ${golden}/reflection-defaults.iprop
                ${golden}/reflection-empty.iprop
                ${golden}/reflection-parts-plain.iprop)
        add_test(NAME capi_render_inputs_${linkage}
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/scripts/check_render_inputs.py
                $<TARGET_FILE:render_inputs_${linkage}>
                ${golden}/uniforms.iprop
                ${golden}/error-trap.iprop)
    endif()
endforeach()

iprop_capi_test(capi_conformance
    test/conformance.c test/golden.c test/errors.c test/generation.c test/test_support.c
    test/png_reader.cpp
    ARGS ${golden} ${CMAKE_SOURCE_DIR}/tests/golden)
target_link_libraries(capi_conformance PRIVATE iprop_png)
endif()
