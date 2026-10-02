# The .ips compiler: AST → .iprop bytes (Phase 20+).

add_library(iprop_compiler STATIC
    Compiler.cpp
    Plan.cpp
    CallResolver.cpp
    Emit.cpp
    EmitExpr.cpp
    EmitCall.cpp
    PaletteLowerer.cpp
    Assemble.cpp)

target_link_libraries(iprop_compiler PUBLIC iprop::runtime ips_lang)
target_include_directories(iprop_compiler PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
iprop_warnings(iprop_compiler)
