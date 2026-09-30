install(TARGETS iprop_godot
    LIBRARY DESTINATION ${CMAKE_INSTALL_DATADIR}/iprop/godot/bin
    RUNTIME DESTINATION ${CMAKE_INSTALL_DATADIR}/iprop/godot/bin)
install(FILES
    ${IPROP_GODOT_PROJECT}/project.godot ${IPROP_GODOT_PROJECT}/demo.gd
    ${IPROP_GODOT_PROJECT}/demo.tscn ${IPROP_GODOT_PROJECT}/conformance.gd
    ${IPROP_GODOT_PROJECT}/iprop.gdextension
    DESTINATION ${CMAKE_INSTALL_DATADIR}/iprop/godot)
install(DIRECTORY ${IPROP_GODOT_PROJECT}/assets
    DESTINATION ${CMAKE_INSTALL_DATADIR}/iprop/godot)
install(FILES ${IPROP_GODOT_PROJECT}/.godot/extension_list.cfg
    DESTINATION ${CMAKE_INSTALL_DATADIR}/iprop/godot/.godot)
# Do not ship a build-machine path to an export template.
set(saved_template "${IPROP_GODOT_EXPORT_TEMPLATE}")
set(IPROP_GODOT_EXPORT_TEMPLATE "")
configure_file(${CMAKE_SOURCE_DIR}/backends/godot/project/export_presets.cfg.in
    ${CMAKE_CURRENT_BINARY_DIR}/installed-export-presets.cfg @ONLY)
set(IPROP_GODOT_EXPORT_TEMPLATE "${saved_template}")
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/installed-export-presets.cfg
    DESTINATION ${CMAKE_INSTALL_DATADIR}/iprop/godot RENAME export_presets.cfg)
install(FILES README.md prepare_macos_template.py
    DESTINATION ${CMAKE_INSTALL_DATADIR}/iprop/godot)
install(FILES "${IPROP_GODOTCPP_DIR}/LICENSE.md"
    DESTINATION ${CMAKE_INSTALL_DATADIR}/iprop/licenses RENAME godot-cpp.md)
