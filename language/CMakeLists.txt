# The .ips language front-end (Part C): lexer, parser, semantic analysis.
# Compiled into ips_lang, shared by the compiler (Phase 20+) and tests.

add_library(ips_lang STATIC
    ips/Lexer.cpp
    ips/SourceError.cpp
    ips/Diagnostic.cpp
    ips/SourceText.cpp
    ips/SourceIdentity.cpp
    ips/Suggestions.cpp
    ips/Parser.cpp
    ips/ParserExpr.cpp
    ips/SourcePrinter.cpp
    ips/Types.cpp
    ips/Typing.cpp
    ips/TypingCalls.cpp
    ips/BuiltinSpec.cpp
    ips/BuiltinArguments.cpp
    ips/CallPlan.cpp
    ips/Fold.cpp
    ips/DeclarationConstants.cpp
    ips/AssetConstants.cpp
    ips/PartTransform.cpp
    ips/Palette.cpp
    ips/Imports.cpp
    ips/Semantics.cpp)

target_link_libraries(ips_lang PUBLIC iprop::runtime)
target_include_directories(ips_lang PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
iprop_warnings(ips_lang)
