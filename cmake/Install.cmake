include(CMakePackageConfigHelpers)

install(TARGETS iprop_runtime iprop_capi iprop_capi_static EXPORT ipropTargets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(FILES backends/capi/iprop.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(FILES backends/capi/iprop.h DESTINATION ${CMAKE_INSTALL_DATADIR}/iprop/backends/capi)
if(NOT WIN32)
    configure_file(cmake/ipropc.pc.in ${CMAKE_CURRENT_BINARY_DIR}/ipropc.pc @ONLY)
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/ipropc.pc DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig)
endif()
# Header-only result reads shared by the engine adapters; includes only iprop.h.
install(FILES backends/ResultQueries.hpp DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/iprop/backend)
# Required by the static target's runtime dependency. C++ headers are
# implementation details; only iprop.h has the documented ABI contract.
install(DIRECTORY runtime/iprop DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    FILES_MATCHING PATTERN "*.hpp")
install(TARGETS ipc ipa ipgen ipval ipdis ipsheet RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

if(APPLE)
    set(iprop_install_rpath "@loader_path/../${CMAKE_INSTALL_LIBDIR}")
elseif(UNIX)
    set(iprop_install_rpath "$ORIGIN/../${CMAKE_INSTALL_LIBDIR}")
endif()
foreach(target ipc ipa ipgen ipval ipdis ipsheet iprop_capi)
    set_target_properties(${target} PROPERTIES INSTALL_RPATH "${iprop_install_rpath}")
endforeach()
if(TARGET iprop_sfml)
    set_target_properties(iprop_sfml PROPERTIES INSTALL_RPATH "${iprop_install_rpath}")
    install(TARGETS iprop_sfml RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    # Include the selected shared SFML libraries. Static SFML needs no
    # runtime artifacts. OS graphics/font dependencies remain system packages.
    foreach(target SFML::Graphics SFML::Window SFML::System)
        get_target_property(type ${target} TYPE)
        if(type STREQUAL "SHARED_LIBRARY")
            install(IMPORTED_RUNTIME_ARTIFACTS ${target}
                RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
                LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})
        endif()
    endforeach()
    get_filename_component(sfml_prefix "${SFML_DIR}/../../.." ABSOLUTE)
    if(EXISTS "${sfml_prefix}/share/doc/SFML/license.md")
        install(FILES "${sfml_prefix}/share/doc/SFML/license.md"
            DESTINATION ${CMAKE_INSTALL_DATADIR}/iprop/licenses RENAME SFML.md)
    endif()
    if(EXISTS "${sfml_prefix}/share/doc/SFML/bundled-notices")
        install(DIRECTORY "${sfml_prefix}/share/doc/SFML/bundled-notices"
            DESTINATION ${CMAKE_INSTALL_DATADIR}/iprop/licenses/SFML)
    endif()
endif()

# Keep the website's compiled showcase programs out of the SDK. Only the
# README preview and its reserved-rights notice are included below.
install(DIRECTORY examples spec docs DESTINATION ${CMAKE_INSTALL_DATADIR}/iprop
    REGEX "/examples/showcase$" EXCLUDE)
install(FILES examples/showcase/readme-collection.png examples/showcase/LICENSE.md
    DESTINATION ${CMAKE_INSTALL_DATADIR}/iprop/examples/showcase)
# The guides keep their source-tree places, so their relative links resolve.
foreach(backend capi html5 sfml godot)
    install(FILES backends/${backend}/README.md
        DESTINATION ${CMAKE_INSTALL_DATADIR}/iprop/backends/${backend})
endforeach()
# Binding source packages are usable independently from the source checkout.
# Mobile native builds additionally need --source/IPROP_SOURCE for the runtime.
set(iprop_binding_patterns)
foreach(extension swift h cpp py toml cs csproj shader asmdef meta json kts kt properties xml pro txt md modulemap)
    list(APPEND iprop_binding_patterns PATTERN "*.${extension}")
endforeach()
foreach(backend apple unity android python)
    install(DIRECTORY backends/${backend}/ DESTINATION ${CMAKE_INSTALL_DATADIR}/iprop/backends/${backend}
        FILES_MATCHING ${iprop_binding_patterns} PATTERN "LICENSE"
        PATTERN ".build" EXCLUDE PATTERN ".swiftpm" EXCLUDE PATTERN "Artifacts" EXCLUDE
        PATTERN ".gradle" EXCLUDE PATTERN ".kotlin" EXCLUDE PATTERN ".cxx" EXCLUDE PATTERN "build" EXCLUDE
        PATTERN "bin" EXCLUDE PATTERN "obj" EXCLUDE PATTERN "Plugins" EXCLUDE
        PATTERN "__pycache__" EXCLUDE PATTERN "*.egg-info" EXCLUDE
        PATTERN "dist" EXCLUDE PATTERN "local.properties" EXCLUDE)
endforeach()
install(FILES backends/test/bindings.ips backends/test/binding-trap.ips backends/test/binding-unicode.ips
    backends/test/prepare-bindings.py backends/test/result_fixture.py backends/test/check-python.py backends/test/check-csharp.py
    backends/test/python-asan.supp
    DESTINATION ${CMAKE_INSTALL_DATADIR}/iprop/backends/test)
foreach(guide editor website)
    install(FILES ${guide}/README.md
        DESTINATION ${CMAKE_INSTALL_DATADIR}/iprop/${guide})
endforeach()
if(Python3_Interpreter_FOUND)
    # Expanded chapters replace their authored directives in installed SDKs.
    install(DIRECTORY "${IPROP_LANGUAGE_DOCS_DIR}/" DESTINATION ${CMAKE_INSTALL_DATADIR}/iprop)
endif()
install(FILES LICENSE README.md CHANGELOG.md DESTINATION ${CMAKE_INSTALL_DATADIR}/iprop)
install(FILES website/public/favicon.svg DESTINATION ${CMAKE_INSTALL_DATADIR}/iprop/website/public)
install(DIRECTORY licenses/ DESTINATION ${CMAKE_INSTALL_DATADIR}/iprop/licenses)
install(FILES third_party/README.md third_party/zlib-LICENSE.txt
    DESTINATION ${CMAKE_INSTALL_DATADIR}/iprop/licenses)

set(iprop_config_dir ${CMAKE_INSTALL_LIBDIR}/cmake/iprop)
configure_package_config_file(cmake/ipropConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/ipropConfig.cmake
    INSTALL_DESTINATION ${iprop_config_dir})
# C API package compatibility follows its independently configured ABI line.
iprop_write_package_version("${CMAKE_CURRENT_BINARY_DIR}" "${PROJECT_VERSION}")
install(EXPORT ipropTargets NAMESPACE iprop:: DESTINATION ${iprop_config_dir})
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/ipropConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/ipropConfigVersion.cmake DESTINATION ${iprop_config_dir})
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/ipropProductVersion.cmake DESTINATION ${iprop_config_dir})
