# The Godot backend (task 8): the GDExtension over the C API.
#
# Pinned versions (task 8.3): Godot 4.5.2 and godot-cpp at
# godot-4.5-stable. Point IPROP_GODOTCPP_DIR at the bindings checkout
# (default: ../.deps/godot-cpp) and IPROP_GODOT_BINARY at the engine
# binary (default: ../.deps/Godot.app/Contents/MacOS/Godot). Without the
# bindings the backend is skipped gracefully.

if(IPROP_GODOT STREQUAL "OFF")
    return()
endif()

set(IPROP_GODOTCPP_DIR
    "${CMAKE_SOURCE_DIR}/../.deps/godot-cpp"
    CACHE PATH "The godot-cpp checkout")

if(NOT EXISTS ${IPROP_GODOTCPP_DIR}/CMakeLists.txt)
    if(IPROP_GODOT STREQUAL "ON")
        message(FATAL_ERROR "Required godot-cpp not found at ${IPROP_GODOTCPP_DIR}")
    endif()
    message(STATUS
        "godot-cpp not found at ${IPROP_GODOTCPP_DIR} — the godot backend is skipped")
    return()
endif()

# The bindings (the generated class headers come from the 4.5-stable API).
add_subdirectory(${IPROP_GODOTCPP_DIR} godot-cpp)

# The extension library (8.1/8.2).
add_library(iprop_godot SHARED
    src/register_types.cpp
    src/iprop_godot.cpp)

target_link_libraries(iprop_godot PRIVATE godot-cpp iprop_capi_static)
target_include_directories(iprop_godot PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/backends/capi)
target_include_directories(iprop_godot SYSTEM PRIVATE
    $<TARGET_PROPERTY:godot-cpp,INTERFACE_INCLUDE_DIRECTORIES>)
iprop_warnings(iprop_godot)

set_target_properties(iprop_godot PROPERTIES
    OUTPUT_NAME "iprop-godot.macos.debug"
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/godot-project/bin)

# The assembled demo project (the sources + the demo assets + the built
# library), ready for the Godot editor or the headless runs.
set(IPROP_GODOT_PROJECT ${CMAKE_BINARY_DIR}/godot-project)
file(MAKE_DIRECTORY ${IPROP_GODOT_PROJECT}/assets)
foreach(file project.godot iprop.gdextension demo.tscn demo.gd conformance.gd)
    configure_file(${CMAKE_CURRENT_SOURCE_DIR}/project/${file}
                   ${IPROP_GODOT_PROJECT}/${file} COPYONLY)
endforeach()
foreach(asset plant.iprop portrait.iprop)
    configure_file(${CMAKE_SOURCE_DIR}/examples/${asset}
                   ${IPROP_GODOT_PROJECT}/assets/${asset} COPYONLY)
endforeach()
# The extension list Godot's editor scan would otherwise generate; without
# it the headless runs (fresh build trees) never load the extension.
file(MAKE_DIRECTORY ${IPROP_GODOT_PROJECT}/.godot)
file(WRITE ${IPROP_GODOT_PROJECT}/.godot/extension_list.cfg
    "res://iprop.gdextension\n")

add_custom_target(godot_demo
    DEPENDS iprop_godot
    COMMENT "Assembling the Godot demo project")

if(IPROP_BUILD_TESTS)
    find_program(IPROP_GODOT_BINARY
        NAMES Godot godot
        HINTS "${CMAKE_SOURCE_DIR}/../.deps/Godot.app/Contents/MacOS"
        CACHE FILEPATH "The Godot engine binary")

    if(IPROP_GODOT_BINARY)
        add_test(NAME godot_conformance
            COMMAND ${IPROP_GODOT_BINARY}
                    --headless
                    --path ${IPROP_GODOT_PROJECT}
                    --script res://conformance.gd
                    -- ${CMAKE_BINARY_DIR}/capi-golden
                       ${CMAKE_SOURCE_DIR}/tests/golden)
        set_tests_properties(godot_conformance PROPERTIES
            DEPENDS capi_golden
            TIMEOUT 120)
    else()
        if(IPROP_GODOT STREQUAL "ON")
            message(FATAL_ERROR "Required Godot engine for conformance was not found")
        endif()
        message(STATUS "the Godot binary not found — the godot conformance is skipped")
    endif()
endif()
