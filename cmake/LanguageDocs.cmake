# Documentation is derived from the tools built in this checkout. Refresh it
# on every full build so doc-only commits cannot retain an older source SHA.
find_package(Python3 3.10 QUIET COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
    set(IPROP_LANGUAGE_DOCS_DIR "${CMAKE_BINARY_DIR}/language-docs" CACHE PATH
        "Generated handbook directory; may be outside a cloud-synchronized build tree")
    add_custom_target(iprop_language_docs ALL
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/scripts/language_docs.py" build
            --compiler "$<TARGET_FILE:ipc>" --output-dir "${IPROP_LANGUAGE_DOCS_DIR}"
        DEPENDS ipc ipgen
        COMMENT "Generate the versioned language handbook"
        VERBATIM)
endif()
