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
    binary_test.cpp
    format_test.cpp
    asset_test.cpp
    code_test.cpp
    validator_test.cpp
    vm_test.cpp
    control_test.cpp
    arithmetic_test.cpp
    seeded_test.cpp
    defense_test.cpp
    utf8_test.cpp
    support/test_support.cpp)

target_link_libraries(iprop_tests PRIVATE
    iprop::runtime
    Catch2::Catch2WithMain)

iprop_warnings(iprop_tests)

add_test(NAME smoke COMMAND iprop_tests)
