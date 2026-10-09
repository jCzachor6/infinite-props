include(CMakePackageConfigHelpers)

install(TARGETS iprop_runtime iprop_capi iprop_capi_static EXPORT ipropTargets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(FILES backends/capi/iprop.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
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

# examples/showcase holds the website's prop binaries, which are not part of
# the SDK.
install(DIRECTORY examples spec docs DESTINATION ${CMAKE_INSTALL_DATADIR}/iprop
    REGEX "/examples/showcase$" EXCLUDE)
# The guides keep their source-tree places, so their relative links resolve.
foreach(backend capi sfml godot)
    install(FILES backends/${backend}/README.md
        DESTINATION ${CMAKE_INSTALL_DATADIR}/iprop/backends/${backend})
endforeach()
if(Python3_Interpreter_FOUND)
    # Expanded chapters replace their authored directives in installed SDKs.
    install(DIRECTORY "${IPROP_LANGUAGE_DOCS_DIR}/" DESTINATION ${CMAKE_INSTALL_DATADIR}/iprop)
endif()
install(FILES LICENSE README.md CHANGELOG.md DESTINATION ${CMAKE_INSTALL_DATADIR}/iprop)
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
