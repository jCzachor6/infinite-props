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
