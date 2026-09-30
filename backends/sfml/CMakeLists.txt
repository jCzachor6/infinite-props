# The SFML host (task 7): a native window over the C API.
#
# SFML 3.1 is discovered with find_package; point it at the base-code
# install when configuring:
#
#   cmake -S . -B build \
#       -DIPROP_SFML_PREFIX=$PWD/../base-code/QuantSpace/.deps/sfml
#
# Without SFML the backend is skipped gracefully.

if(IPROP_SFML STREQUAL "OFF")
    return()
endif()

if(DEFINED IPROP_SFML_PREFIX)
    list(PREPEND CMAKE_PREFIX_PATH ${IPROP_SFML_PREFIX})
endif()

if(IPROP_SFML STREQUAL "ON")
    find_package(SFML 3.1 REQUIRED GLOBAL COMPONENTS Graphics Window System)
else()
    find_package(SFML 3.1 QUIET GLOBAL COMPONENTS Graphics Window System)
endif()

if(SFML_FOUND)
    # 7.1/7.3: the windowed host.
    add_executable(iprop_sfml
        src/host.cpp)

    target_link_libraries(iprop_sfml PRIVATE iprop_capi_static SFML::Graphics)
    target_include_directories(iprop_sfml PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    iprop_warnings(iprop_sfml)

    if(IPROP_BUILD_TESTS)
        add_executable(sfml_render test/render.cpp)
        target_link_libraries(sfml_render PRIVATE SFML::Graphics)
        target_include_directories(sfml_render PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
        iprop_warnings(sfml_render)
        add_test(NAME sfml_render COMMAND sfml_render)
        add_test(NAME sfml_errors COMMAND ${CMAKE_COMMAND}
            -DHOST=$<TARGET_FILE:iprop_sfml>
            -DPROGRAM=${CMAKE_SOURCE_DIR}/examples/portrait.iprop
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/error-test.png
            -P ${CMAKE_CURRENT_SOURCE_DIR}/test/errors.cmake)
        # 7.2: the headless conformance over the corpus assembled by the
        # capi_golden target (backends/capi).
        add_executable(sfml_conformance
            test/conformance.cpp)

        target_link_libraries(sfml_conformance PRIVATE iprop_capi_static SFML::Graphics)
        target_include_directories(sfml_conformance PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
        iprop_warnings(sfml_conformance)
        add_dependencies(sfml_conformance capi_golden)

        add_test(NAME sfml_conformance
            COMMAND sfml_conformance
                    ${CMAKE_BINARY_DIR}/capi-golden
                    ${CMAKE_SOURCE_DIR}/tests/golden)
    endif()
else()
    message(STATUS "SFML 3.1 not found — the sfml backend is skipped")
endif()
