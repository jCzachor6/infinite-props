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
    stb_image_impl.cpp
    lexer_test.cpp
    language/parser_assets_test.cpp
    language/parser_statements_test.cpp
    language/parser_diagnostics_test.cpp
    language/draw_entry_test.cpp
    language/color_namespace_test.cpp
    language/aliases_test.cpp
    language/random_sugar_test.cpp
    language/named_arguments_test.cpp
    language/direct_seed_test.cpp
    language/diagnostic_hints_test.cpp
    language/source_printer_test.cpp
    language/recipe_gallery_test.cpp
    language/emoji_aliases_test.cpp
    printer_test.cpp
    language/semantics_types_test.cpp
    language/semantics_functions_test.cpp
    language/semantics_parts_test.cpp
    compiler/basics_test.cpp
    compiler/scalar_test.cpp
    compiler/control_flow_test.cpp
    compiler/vectors_test.cpp
    compiler/palettes_test.cpp
    compiler/drawing_test.cpp
    compiler/part_transforms_test.cpp
    compiler/parts_test.cpp
    compiler/calls_test.cpp
    builtin_spec_test.cpp
    typed_plan_test.cpp
    palette_lowering_test.cpp
    disasm_test.cpp
    debug_test.cpp
    assembler_test.cpp
    portrait_test.cpp
    golden_test.cpp
    binary_test.cpp
    format_test.cpp
    asset_test.cpp
    code_test.cpp
    validation/opcodes_test.cpp
    validation/functions_test.cpp
    validation/parts_test.cpp
    opcode_spec_test.cpp
    vm_test.cpp
    control_test.cpp
    control_validation_test.cpp
    math/integer_test.cpp
    math/float_test.cpp
    math/trigonometry_test.cpp
    math/vectors_test.cpp
    math/distance_test.cpp
    math/noise_test.cpp
    math/curves_test.cpp
    math/colors_test.cpp
    vector_determinism_test.cpp
    bezier_kernel_test.cpp
    seeded_test.cpp
    canvas_test.cpp
    outputs_test.cpp
    parts_test.cpp
    part_geometry_test.cpp
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
    Catch2::Catch2WithMain)

target_include_directories(iprop_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_include_directories(iprop_tests SYSTEM PRIVATE
    ${CMAKE_SOURCE_DIR}/third_party/stb)

iprop_warnings(iprop_tests)

add_test(NAME smoke COMMAND iprop_tests)

find_program(IPROP_NODE_EXECUTABLE NAMES node)
if(IPROP_NODE_EXECUTABLE)
    add_test(NAME editor_diagnostics COMMAND ${CMAKE_COMMAND} -E env
        "IPROP_COMPILER=$<TARGET_FILE:ipc>"
        ${IPROP_NODE_EXECUTABLE} --test "${CMAKE_SOURCE_DIR}/editor/test/diagnostics.test.js")
endif()
