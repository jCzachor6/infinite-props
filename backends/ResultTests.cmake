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
