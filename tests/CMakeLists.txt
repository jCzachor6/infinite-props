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
    parser_test.cpp
    printer_test.cpp
    semantics_test.cpp
    compiler_test.cpp
    disasm_test.cpp
    debug_test.cpp
    assembler_test.cpp
    portrait_test.cpp
    golden_test.cpp
    binary_test.cpp
    format_test.cpp
    asset_test.cpp
    code_test.cpp
    validator_test.cpp
    vm_test.cpp
    control_test.cpp
    control_validation_test.cpp
    arithmetic_test.cpp
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
