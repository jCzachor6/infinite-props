# ipc: compile .ips source into an .iprop binary

add_executable(ipc
    ipc.cpp
    source_diagnostics.cpp
    language_description.cpp)

target_link_libraries(ipc PRIVATE iprop_compiler)
target_compile_definitions(ipc PRIVATE IPROP_PRODUCT_VERSION="${PROJECT_VERSION}"
    IPROP_C_ABI_VERSION_MAJOR=${IPROP_C_ABI_VERSION_MAJOR}
    IPROP_C_ABI_VERSION_MINOR=${IPROP_C_ABI_VERSION_MINOR})
iprop_warnings(ipc)

# ipdis: disassemble an .iprop binary into .ipa text
# ipval: validate an .iprop binary with a machine-readable report

add_library(ipdis_lib STATIC
    ipdis.cpp)

target_link_libraries(ipdis_lib PUBLIC iprop::runtime)
target_include_directories(ipdis_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
iprop_warnings(ipdis_lib)

add_executable(ipdis
    ipdis_main.cpp)

target_link_libraries(ipdis PRIVATE ipdis_lib)
iprop_warnings(ipdis)

add_library(ipval_lib STATIC
    ipval.cpp)

target_link_libraries(ipval_lib PUBLIC iprop::runtime)
target_include_directories(ipval_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
iprop_warnings(ipval_lib)

add_executable(ipval
    ipval_main.cpp)

target_link_libraries(ipval PRIVATE ipval_lib)
iprop_warnings(ipval)

# ipgen: generate a seed-based procedural asset (.iprop → PNG + JSON)
# ipa: assemble .ipa text into .iprop binary
# ipsheet: render a variant × seed grid PNG

add_library(ipgen_lib STATIC
    ipgen.cpp)

target_link_libraries(ipgen_lib PUBLIC iprop::runtime)
target_include_directories(ipgen_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(ipgen_lib SYSTEM PUBLIC
    ${CMAKE_SOURCE_DIR}/third_party/stb)
iprop_warnings(ipgen_lib)

add_executable(ipgen
    main.cpp)

target_link_libraries(ipgen PRIVATE ipgen_lib)
iprop_warnings(ipgen)

add_library(ipa_lib STATIC
    ipa.cpp)

target_link_libraries(ipa_lib PUBLIC iprop::runtime)
target_include_directories(ipa_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
iprop_warnings(ipa_lib)

add_executable(ipa
    ipa_main.cpp)

target_link_libraries(ipa PRIVATE ipa_lib)
iprop_warnings(ipa)

add_library(ipsheet_lib STATIC
    ipsheet.cpp)

target_link_libraries(ipsheet_lib PUBLIC iprop::runtime)
target_include_directories(ipsheet_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(ipsheet_lib SYSTEM PUBLIC
    ${CMAKE_SOURCE_DIR}/third_party/stb)
iprop_warnings(ipsheet_lib)

add_executable(ipsheet
    ipsheet_main.cpp)

target_link_libraries(ipsheet PRIVATE ipsheet_lib)
iprop_warnings(ipsheet)
