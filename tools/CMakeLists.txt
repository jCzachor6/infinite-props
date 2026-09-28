# ipgen: generate a seed-based procedural asset (.iprop → PNG + JSON)

add_library(ipgen_lib STATIC
    ipgen.cpp)

target_link_libraries(ipgen_lib PUBLIC iprop::runtime)
target_include_directories(ipgen_lib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR})

# Vendored stb: third-party code, warnings suppressed via SYSTEM.
target_include_directories(ipgen_lib SYSTEM PUBLIC
    ${CMAKE_SOURCE_DIR}/third_party/stb)

iprop_warnings(ipgen_lib)

add_executable(ipgen
    main.cpp)

target_link_libraries(ipgen PRIVATE ipgen_lib)
iprop_warnings(ipgen)
