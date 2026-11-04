include(FetchContent)

if(IPROP_FETCH_DEPS)
    FetchContent_Declare(
        Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG v3.7.1
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(Catch2)
endif()

add_executable(iprop_tests
    smoke_test.cpp
    dynamic_parts_test.cpp
    part_outputs_test.cpp
    lexer_test.cpp
    language/parser_assets_test.cpp
    language/parser_statements_test.cpp
    language/parser_diagnostics_test.cpp
    language/parser_recovery_test.cpp
    language/draw_entry_test.cpp
    language/color_namespace_test.cpp
    language/aliases_test.cpp
    language/random_sugar_test.cpp
    language/reserved_keywords_test.cpp
    language/named_arguments_test.cpp
    language/direct_seed_test.cpp
    language/diagnostic_hints_test.cpp
    language/source_printer_test.cpp
    language/recipe_gallery_test.cpp
    language/semantics_types_test.cpp
    language/semantics_functions_test.cpp
    language/semantics_parts_test.cpp
    compiler/basics_test.cpp
    compiler/optimizer_test.cpp
    compiler/scalar_test.cpp
    compiler/math_bounds_test.cpp
    compiler/asset_constants_test.cpp
    compiler/collections_test.cpp
    compiler/list_methods_test.cpp
    compiler/samplers_test.cpp
    compiler/static_values_test.cpp
    compiler/review_regressions_test.cpp
    compiler/steppers_test.cpp
    compiler/sampling_palettes_test.cpp
    compiler/defaults_test.cpp
    compiler/declared_inputs_test.cpp
    compiler/source_compilation_test.cpp
    compiler/control_flow_test.cpp
    compiler/vectors_test.cpp
    compiler/vector_overloads_test.cpp
    compiler/palettes_test.cpp
    compiler/materials_test.cpp
    compiler/drawing_test.cpp
    compiler/color_channels_test.cpp
    compiler/random_ranges_test.cpp
    compiler/distributions_test.cpp
    compiler/point_distributions_test.cpp
    compiler/density_test.cpp
    compiler/surfaces_test.cpp
    compiler/source_over_test.cpp
    compiler/masks_test.cpp
    compiler/noise_masks_test.cpp
    compiler/blur_test.cpp
    compiler/resource_exports_test.cpp
    compiler/gradients_test.cpp
    compiler/part_transforms_test.cpp
    compiler/parts_test.cpp
    compiler/nested_parts_test.cpp
    compiler/calls_test.cpp
    compiler/effects_test.cpp
    builtin_spec_test.cpp
    typed_plan_test.cpp
    call_plan_test.cpp
    palette_lowering_test.cpp
    disasm_test.cpp
    debug_test.cpp
    error_test.cpp
    assembler_test.cpp
    portrait_test.cpp
    golden_test.cpp
    binary_test.cpp
    format_test.cpp
    compat_v08_ids_test.cpp
    compression_test.cpp
    asset_test.cpp
    code_test.cpp
    validation/opcodes_test.cpp
    validation/functions_test.cpp
    validation/parts_test.cpp
    opcode_spec_test.cpp
    vm_test.cpp
    vm_result_test.cpp
    execution_limits_test.cpp
    control_test.cpp
    control_validation_test.cpp
    math/integer_test.cpp
    math/float_test.cpp
    math/trigonometry_test.cpp
    math/vectors_test.cpp
    math/distance_test.cpp
    math/noise_test.cpp
    math/noise_extensions_test.cpp
    math/curves_test.cpp
    math/colors_test.cpp
    vector_determinism_test.cpp
    bezier_kernel_test.cpp
    seeded_test.cpp
    canvas_test.cpp
    outputs_test.cpp
    parts_test.cpp
    part_geometry_test.cpp
    geometry_test.cpp
    voronoi_test.cpp
    contours_test.cpp
    regions_test.cpp
    gap_bevel_test.cpp
    tools_test.cpp
    defense_test.cpp
    utf8_test.cpp
    support/compiler_support.cpp
    support/semantic_support.cpp
    support/validation_support.cpp
    support/test_support.cpp)

target_link_libraries(iprop_tests PRIVATE
    ipgen_lib
    ipa_lib
    ipsheet_lib
    ipdis_lib
    ipval_lib
    ips_lang
    iprop_compiler
    iprop_png
    Catch2::Catch2WithMain)

target_include_directories(iprop_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

iprop_warnings(iprop_tests)

add_test(NAME smoke COMMAND iprop_tests)

# Keep allocator replacement outside the Catch2 process. Sweep the actual
# formatter allocations, including persistent failure during scope unwinding.
add_executable(source_printer_failure_probe language/source_printer_failure_probe.cpp)
target_link_libraries(source_printer_failure_probe PRIVATE ips_lang)
iprop_warnings(source_printer_failure_probe)
add_test(NAME source_printer_allocation_failures COMMAND source_printer_failure_probe)

add_executable(vm_result_allocation_probe vm_result_allocation_probe.cpp)
target_link_libraries(vm_result_allocation_probe PRIVATE iprop_compiler)
iprop_warnings(vm_result_allocation_probe)
add_test(NAME vm_result_allocations COMMAND vm_result_allocation_probe)

add_executable(blur_allocation_probe compiler/blur_allocation_probe.cpp)
target_link_libraries(blur_allocation_probe PRIVATE iprop_runtime)
iprop_warnings(blur_allocation_probe)
add_test(NAME blur_allocation_failures COMMAND blur_allocation_probe)

add_executable(geometry_allocation_probe geometry_allocation_probe.cpp)
target_link_libraries(geometry_allocation_probe PRIVATE iprop_runtime)
iprop_warnings(geometry_allocation_probe)
add_test(NAME geometry_allocation_failures COMMAND geometry_allocation_probe)

add_executable(geometry_representation_probe geometry_representation_probe.cpp)
target_link_libraries(geometry_representation_probe PRIVATE iprop_runtime)
iprop_warnings(geometry_representation_probe)
add_executable(voronoi_probe voronoi_probe.cpp)
target_link_libraries(voronoi_probe PRIVATE iprop_runtime)
iprop_warnings(voronoi_probe)
add_executable(voronoi_allocation_probe voronoi_allocation_probe.cpp)
target_link_libraries(voronoi_allocation_probe PRIVATE iprop_runtime)
iprop_warnings(voronoi_allocation_probe)
add_test(NAME voronoi_allocation_failures COMMAND voronoi_allocation_probe)

add_executable(regions_allocation_probe regions_allocation_probe.cpp)
target_link_libraries(regions_allocation_probe PRIVATE iprop_runtime)
iprop_warnings(regions_allocation_probe)
add_test(NAME regions_allocation_failures COMMAND regions_allocation_probe)

add_executable(regions_probe regions_probe.cpp)
target_link_libraries(regions_probe PRIVATE iprop_runtime)
iprop_warnings(regions_probe)

add_executable(gap_bevel_probe gap_bevel_probe.cpp)
target_link_libraries(gap_bevel_probe PRIVATE iprop_runtime)
iprop_warnings(gap_bevel_probe)

add_executable(dynamic_parts_probe dynamic_parts_probe.cpp)
target_link_libraries(dynamic_parts_probe PRIVATE iprop_runtime)
iprop_warnings(dynamic_parts_probe)

add_executable(execution_limits_probe execution_limits_probe.cpp)
target_link_libraries(execution_limits_probe PRIVATE iprop_capi_static)
iprop_warnings(execution_limits_probe)

add_executable(part_exports_probe part_exports_probe.cpp)
target_link_libraries(part_exports_probe PRIVATE iprop_capi_static)
iprop_warnings(part_exports_probe)

add_executable(dynamic_parts_allocation_probe dynamic_parts_allocation_probe.cpp)
target_link_libraries(dynamic_parts_allocation_probe PRIVATE iprop_runtime)
iprop_warnings(dynamic_parts_allocation_probe)
add_test(NAME dynamic_parts_allocation_failures COMMAND dynamic_parts_allocation_probe)

add_executable(gap_bevel_allocation_probe gap_bevel_allocation_probe.cpp)
target_link_libraries(gap_bevel_allocation_probe PRIVATE iprop_runtime)
iprop_warnings(gap_bevel_allocation_probe)
add_test(NAME gap_bevel_allocation_failures COMMAND gap_bevel_allocation_probe)

add_executable(contours_probe contours_probe.cpp)
target_link_libraries(contours_probe PRIVATE iprop_runtime)
iprop_warnings(contours_probe)
add_executable(contours_allocation_probe contours_allocation_probe.cpp)
target_link_libraries(contours_allocation_probe PRIVATE iprop_runtime)
iprop_warnings(contours_allocation_probe)
add_test(NAME contours_allocation_failures COMMAND contours_allocation_probe)

add_executable(compiler_diagnostic_failure_probe compiler/diagnostic_failure_probe.cpp
    ../tools/source_diagnostics.cpp ../tools/language_description.cpp)
target_link_libraries(compiler_diagnostic_failure_probe PRIVATE iprop_compiler)
target_compile_definitions(compiler_diagnostic_failure_probe PRIVATE IPROP_PRODUCT_VERSION="${PROJECT_VERSION}"
    IPROP_C_ABI_VERSION_MAJOR=${IPROP_C_ABI_VERSION_MAJOR}
    IPROP_C_ABI_VERSION_MINOR=${IPROP_C_ABI_VERSION_MINOR}
    IPROP_C_ABI_SOVERSION="${IPROP_C_ABI_SOVERSION}")
iprop_warnings(compiler_diagnostic_failure_probe)

find_program(IPROP_NODE_EXECUTABLE NAMES node)
if(IPROP_NODE_EXECUTABLE)
    add_test(NAME part_exports_v09 COMMAND ${IPROP_NODE_EXECUTABLE}
        "${CMAKE_SOURCE_DIR}/backends/html5/test/part-exports-conformance.js" "$<TARGET_FILE_DIR:ipc>"
        "$<TARGET_FILE:part_exports_probe>")
    add_test(NAME dynamic_parts_v09 COMMAND ${IPROP_NODE_EXECUTABLE}
        "${CMAKE_SOURCE_DIR}/backends/html5/test/dynamic-parts-conformance.js" "$<TARGET_FILE_DIR:ipc>"
        "$<TARGET_FILE:dynamic_parts_probe>")
    add_test(NAME gap_bevel_v09 COMMAND ${IPROP_NODE_EXECUTABLE}
        "${CMAKE_SOURCE_DIR}/backends/html5/test/gap-bevel-conformance.js" "$<TARGET_FILE_DIR:ipc>"
        "$<TARGET_FILE:gap_bevel_probe>")
    add_test(NAME regions_v09 COMMAND ${IPROP_NODE_EXECUTABLE}
        "${CMAKE_SOURCE_DIR}/backends/html5/test/regions-conformance.js" "$<TARGET_FILE_DIR:ipc>"
        "$<TARGET_FILE:regions_probe>")
    add_test(NAME contours_v09 COMMAND ${IPROP_NODE_EXECUTABLE}
        "${CMAKE_SOURCE_DIR}/backends/html5/test/contours-conformance.js" "$<TARGET_FILE_DIR:ipc>"
        "$<TARGET_FILE:contours_probe>")
    add_test(NAME voronoi_v09 COMMAND ${IPROP_NODE_EXECUTABLE}
        "${CMAKE_SOURCE_DIR}/backends/html5/test/voronoi-conformance.js" "$<TARGET_FILE_DIR:ipc>"
        "$<TARGET_FILE:voronoi_probe>")
    add_test(NAME geometry_v09 COMMAND ${IPROP_NODE_EXECUTABLE}
        "${CMAKE_SOURCE_DIR}/backends/html5/test/geometry-v09-conformance.js" "$<TARGET_FILE_DIR:ipc>"
        "$<TARGET_FILE:geometry_representation_probe>")
    add_test(NAME noise_mask_v09 COMMAND ${IPROP_NODE_EXECUTABLE}
        "${CMAKE_SOURCE_DIR}/backends/html5/test/noise-mask-conformance.js" "$<TARGET_FILE_DIR:ipc>")
    add_test(NAME noise_v09 COMMAND ${IPROP_NODE_EXECUTABLE}
        "${CMAKE_SOURCE_DIR}/backends/html5/test/noise-v09-conformance.js" "$<TARGET_FILE_DIR:ipc>")
    add_test(NAME compatibility_v08 COMMAND ${IPROP_NODE_EXECUTABLE}
        "${CMAKE_SOURCE_DIR}/backends/html5/test/compat-v08-conformance.js" "$<TARGET_FILE_DIR:ipc>")
    add_test(NAME runtime_matrix_comparators COMMAND ${IPROP_NODE_EXECUTABLE}
        --test "${CMAKE_SOURCE_DIR}/scripts/tests/runtime_matrix_wasm.test.js")
    add_test(NAME editor_diagnostics COMMAND ${CMAKE_COMMAND} -E env
        "IPROP_COMPILER=$<TARGET_FILE:ipc>"
        ${IPROP_NODE_EXECUTABLE} --test "${CMAKE_SOURCE_DIR}/editor/test/diagnostics.test.js")
    add_test(NAME compiler_output_defaults COMMAND ${CMAKE_COMMAND} -E env
        "IPROP_COMPILER=$<TARGET_FILE:ipc>"
        ${IPROP_NODE_EXECUTABLE} --test "${CMAKE_SOURCE_DIR}/editor/test/production.test.js")
    add_test(NAME editor_formatting COMMAND ${CMAKE_COMMAND} -E env
        "IPROP_COMPILER=$<TARGET_FILE:ipc>"
        ${IPROP_NODE_EXECUTABLE} --test "${CMAKE_SOURCE_DIR}/editor/test/formatting.test.js")
endif()

if(Python3_Interpreter_FOUND)
    add_test(NAME binding_python COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_SOURCE_DIR}/backends/test/check-python.py
        $<TARGET_FILE_DIR:ipc> $<TARGET_FILE:iprop_capi> ${CMAKE_BINARY_DIR}/binding-fixtures)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU"
            AND CMAKE_CXX_FLAGS MATCHES "-fsanitize=[^ ]*address")
        execute_process(COMMAND "${CMAKE_CXX_COMPILER}" -print-file-name=libasan.so
            RESULT_VARIABLE iprop_asan_status OUTPUT_VARIABLE iprop_asan_runtime
            OUTPUT_STRIP_TRAILING_WHITESPACE)
        execute_process(COMMAND "${CMAKE_CXX_COMPILER}" -print-file-name=libstdc++.so
            RESULT_VARIABLE iprop_cxx_status OUTPUT_VARIABLE iprop_cxx_runtime
            OUTPUT_STRIP_TRAILING_WHITESPACE)
        if(NOT iprop_asan_status EQUAL 0 OR NOT EXISTS "${iprop_asan_runtime}"
                OR NOT iprop_cxx_status EQUAL 0 OR NOT EXISTS "${iprop_cxx_runtime}")
            message(FATAL_ERROR "Python binding sanitizer tests require the GCC ASan and C++ runtimes")
        endif()
        add_library(python_sanitizer_probe SHARED python_sanitizer_probe.cpp)
        set_property(TEST binding_python APPEND PROPERTY ENVIRONMENT
            "IPROP_SANITIZER_PRELOAD=${iprop_asan_runtime}:${iprop_cxx_runtime}"
            "IPROP_SANITIZER_PROBE=$<TARGET_FILE:python_sanitizer_probe>")
    endif()
    add_test(NAME mask_rational_reference COMMAND ${Python3_EXECUTABLE}
        "${CMAKE_SOURCE_DIR}/scripts/check_mask_reference.py")
    add_test(NAME source_over_rational_reference COMMAND ${Python3_EXECUTABLE}
        "${CMAKE_SOURCE_DIR}/scripts/check_source_over_reference.py")
    add_test(NAME retired_format_rejection COMMAND ${Python3_EXECUTABLE}
        "${CMAKE_SOURCE_DIR}/scripts/check_frozen_math.py" "$<TARGET_FILE:ipgen>")
    add_test(NAME resource_export_reference COMMAND ${Python3_EXECUTABLE}
        "${CMAKE_SOURCE_DIR}/scripts/check_resource_export_reference.py")
    add_test(NAME compiler_diagnostic_failures COMMAND ${Python3_EXECUTABLE}
        "${CMAKE_SOURCE_DIR}/scripts/check_diagnostic_failures.py" "$<TARGET_FILE:compiler_diagnostic_failure_probe>")
    add_test(NAME compiler_check_cli COMMAND ${Python3_EXECUTABLE}
        "${CMAKE_SOURCE_DIR}/scripts/check_source_cli.py" "$<TARGET_FILE:ipc>")
    add_test(NAME compiler_format_cli COMMAND ${Python3_EXECUTABLE}
        "${CMAKE_SOURCE_DIR}/scripts/check_format_cli.py" "$<TARGET_FILE:ipc>")
    add_test(NAME compiler_materials_cli COMMAND ${Python3_EXECUTABLE}
        "${CMAKE_SOURCE_DIR}/scripts/check_materials_cli.py" "$<TARGET_FILE:ipc>" "$<TARGET_FILE:ipgen>")
    add_test(NAME recipe_format_cli COMMAND ${Python3_EXECUTABLE}
        "${CMAKE_SOURCE_DIR}/scripts/check_recipe_format.py" "$<TARGET_FILE:ipc>")
    add_test(NAME compiler_export_contracts COMMAND ${Python3_EXECUTABLE}
        "${CMAKE_SOURCE_DIR}/scripts/check_export_contracts.py" "$<TARGET_FILE:ipc>"
        --probe "$<TARGET_FILE:compiler_diagnostic_failure_probe>")
    add_test(NAME compiler_language_contract COMMAND ${Python3_EXECUTABLE}
        "${CMAKE_SOURCE_DIR}/scripts/check_language_contract.py" "$<TARGET_FILE:ipc>")
    add_test(NAME compiler_language_effects COMMAND ${Python3_EXECUTABLE}
        "${CMAKE_SOURCE_DIR}/scripts/check_language_effects.py" "$<TARGET_FILE:ipc>")
    add_test(NAME ci_release_tools COMMAND ${Python3_EXECUTABLE} -m unittest discover
        -s "${CMAKE_SOURCE_DIR}/scripts/tests" -p "test_*.py")
    add_test(NAME compiler_optimization_acceptance COMMAND ${Python3_EXECUTABLE}
        "${CMAKE_SOURCE_DIR}/scripts/check-optimizations.py" "$<TARGET_FILE_DIR:ipc>")
    add_test(NAME language_documentation COMMAND ${Python3_EXECUTABLE}
        "${CMAKE_SOURCE_DIR}/scripts/language_docs.py" check --compiler "$<TARGET_FILE:ipc>"
        --report-dir "${CMAKE_BINARY_DIR}/ci-reports/language")
    add_test(NAME language_llm_reference COMMAND ${Python3_EXECUTABLE}
        "${CMAKE_SOURCE_DIR}/scripts/check_llm_reference.py" "$<TARGET_FILE:ipc>")
endif()
