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
    arithmetic_test.cpp
    seeded_test.cpp
    canvas_test.cpp
    outputs_test.cpp
    parts_test.cpp
    tools_test.cpp
    defense_test.cpp
    utf8_test.cpp
    support/test_support.cpp)

target_link_libraries(iprop_tests PRIVATE
    ipgen_lib
    ipa_lib
    ipsheet_lib
    ips_lang
    Catch2::Catch2WithMain)

target_include_directories(iprop_tests SYSTEM PRIVATE
    ${CMAKE_SOURCE_DIR}/third_party/stb)

iprop_warnings(iprop_tests)

add_test(NAME smoke COMMAND iprop_tests)
