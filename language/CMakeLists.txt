# The .ips language front-end (Part C): lexer, parser, semantic analysis.
# Compiled into ips_lang, shared by the compiler (Phase 20+) and tests.

add_library(ips_lang STATIC
    ips/Lexer.cpp
    ips/SourceError.cpp
    ips/Parser.cpp
    ips/ParserExpr.cpp
    ips/Printer.cpp
    ips/Types.cpp
    ips/Semantics.cpp)

target_link_libraries(ips_lang PUBLIC iprop::runtime)
target_include_directories(ips_lang PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
iprop_warnings(ips_lang)
