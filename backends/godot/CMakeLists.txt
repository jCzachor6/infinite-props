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
if(NOT DEFINED GODOTCPP_TARGET)
    if(CMAKE_BUILD_TYPE MATCHES "^(Release|MinSizeRel)$")
        set(GODOTCPP_TARGET template_release CACHE STRING "Godot bindings target")
    else()
        set(GODOTCPP_TARGET template_debug CACHE STRING "Godot bindings target")
    endif()
endif()
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

if(APPLE)
    set(IPROP_GODOT_PLATFORM macos)
    set(IPROP_GODOT_EXPORT_PLATFORM macOS)
elseif(WIN32)
    set(IPROP_GODOT_PLATFORM windows)
    set(IPROP_GODOT_EXPORT_PLATFORM "Windows Desktop")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(IPROP_GODOT_PLATFORM linux)
    set(IPROP_GODOT_EXPORT_PLATFORM Linux)
else()
    message(FATAL_ERROR "The Godot backend supports macOS, Windows and Linux")
endif()
get_target_property(IPROP_GODOT_ARCH godot-cpp GODOTCPP_ARCH)
if(GODOTCPP_TARGET STREQUAL "template_release")
    set(IPROP_GODOT_FLAVOR release)
else()
    set(IPROP_GODOT_FLAVOR debug)
endif()
set(IPROP_GODOT_NAME "iprop-godot.${IPROP_GODOT_PLATFORM}.${IPROP_GODOT_FLAVOR}.${IPROP_GODOT_ARCH}")
set(IPROP_GODOT_LIBRARY "${CMAKE_SHARED_LIBRARY_PREFIX}${IPROP_GODOT_NAME}${CMAKE_SHARED_LIBRARY_SUFFIX}")
set_target_properties(iprop_godot PROPERTIES
    OUTPUT_NAME "${IPROP_GODOT_NAME}"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/godot-project/bin"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/godot-project/bin")

# The assembled demo project (the sources + the demo assets + the built
# library), ready for the Godot editor or the headless runs.
set(IPROP_GODOT_PROJECT ${CMAKE_BINARY_DIR}/godot-project)
file(MAKE_DIRECTORY ${IPROP_GODOT_PROJECT}/assets)
foreach(file project.godot demo.tscn demo.gd conformance.gd)
    configure_file(${CMAKE_CURRENT_SOURCE_DIR}/project/${file}
                   ${IPROP_GODOT_PROJECT}/${file} COPYONLY)
endforeach()
configure_file(project/iprop.gdextension.in ${IPROP_GODOT_PROJECT}/iprop.gdextension @ONLY)
set(IPROP_GODOT_RELEASE_TEMPLATE "" CACHE FILEPATH "Optional Godot release export template")
set(IPROP_GODOT_EXPORT_TEMPLATE "${IPROP_GODOT_RELEASE_TEMPLATE}")
if(APPLE AND IPROP_GODOT_RELEASE_TEMPLATE AND NOT IPROP_GODOT_ARCH STREQUAL "universal")
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    set(IPROP_GODOT_EXPORT_TEMPLATE "${CMAKE_CURRENT_BINARY_DIR}/macos-template.${IPROP_GODOT_ARCH}.zip")
    add_custom_command(OUTPUT "${IPROP_GODOT_EXPORT_TEMPLATE}"
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/prepare_macos_template.py
                "${IPROP_GODOT_RELEASE_TEMPLATE}" "${IPROP_GODOT_ARCH}" "${IPROP_GODOT_EXPORT_TEMPLATE}"
        DEPENDS "${IPROP_GODOT_RELEASE_TEMPLATE}" prepare_macos_template.py)
    add_custom_target(godot_export_template DEPENDS "${IPROP_GODOT_EXPORT_TEMPLATE}")
    add_dependencies(iprop_godot godot_export_template)
endif()
configure_file(project/export_presets.cfg.in ${IPROP_GODOT_PROJECT}/export_presets.cfg @ONLY)
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
