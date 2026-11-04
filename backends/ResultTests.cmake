# Shared fixtures for the real SFML and Godot result adapters.
if(NOT TARGET backend_result_fixtures)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    set(raw ${CMAKE_BINARY_DIR}/capi-golden/backend-raw-values.iprop)
    set(empty ${CMAKE_BINARY_DIR}/capi-golden/backend-empty-values.iprop)
    add_custom_command(OUTPUT ${raw} ${empty}
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_LIST_DIR}/test/result_fixture.py
                ${CMAKE_BINARY_DIR}/capi-golden
        DEPENDS ${CMAKE_CURRENT_LIST_DIR}/test/result_fixture.py)
    add_custom_target(backend_result_fixtures DEPENDS ${raw} ${empty})
endif()

if(NOT TARGET backend_part_fixtures)
    set(part_fixtures ${CMAKE_BINARY_DIR}/binding-fixtures/part-bindings.json)
    file(GLOB part_sources CONFIGURE_DEPENDS
        ${CMAKE_SOURCE_DIR}/tests/fixtures/part-exports-*.ips
        ${CMAKE_SOURCE_DIR}/backends/capi/test/part-output-*.ips)
    add_custom_command(OUTPUT ${part_fixtures}
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_LIST_DIR}/test/part_bindings.py
            $<TARGET_FILE_DIR:ipc> ${CMAKE_BINARY_DIR}/binding-fixtures
        DEPENDS ipc ${part_sources}
            ${CMAKE_CURRENT_LIST_DIR}/test/part_bindings.py
            ${CMAKE_CURRENT_LIST_DIR}/test/part-bindings-v0.9.json
            ${CMAKE_SOURCE_DIR}/tests/fixtures/part-exports-v0.9.json)
    add_custom_target(backend_part_fixtures DEPENDS ${part_fixtures})
endif()
