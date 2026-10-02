#include <catch2/catch_test_macros.hpp>

#include <string>

#include "ips/Ast.hpp"
#include "ips/Parser.hpp"
#include "ips/SourceError.hpp"

// The .ips parser: sketch grammars parse into the expected AST, malformed
// programs produce exactly one located error, and edge-case grammar forms
// (semicolons, else-if, soft keywords as values) are pinned.

using namespace iprop::ips;

TEST_CASE("untyped parameters are rejected pre-1.0") {
    // The language breaks backward compatibility before the 1.0 freeze:
    // every parameter must declare its type.
    struct Case {
        const char* source;
        const char* messagePart;
    };
    const Case cases[] = {
        {"asset m { draw(v, a) { } }", "':' and a parameter type"},
        {"asset m { draw(v: i32, a) { } }", "':' and a parameter type"},
        {"asset m { f(x, y: f32) { } }", "':' and a parameter type"},
        {"asset m { f(x) -> i32 { return 1 } }", "':' and a parameter type"},
    };
    for (const Case& testCase : cases) {
        INFO(testCase.source);
        bool threw = false;
        try {
            (void)parse(testCase.source);
        } catch (const SourceError& error) {
            threw = true;
            REQUIRE(std::string(error.message).find(testCase.messagePart) !=
                    std::string::npos);
        }
        REQUIRE(threw);
    }
}

TEST_CASE("u32 parameters are rejected") {
    bool threw = false;
    try {
        (void)parse("asset u { draw(v: u32, a: i32) { } }");
    } catch (const SourceError& error) {
        threw = true;
        REQUIRE(std::string(error.message).find("uniform-only") !=
                std::string::npos);
    }
    REQUIRE(threw);
}

TEST_CASE("malformed parameter types produce one located error") {
    struct Case {
        const char* source;
        const char* messagePart;
    };
    const Case cases[] = {
        {"asset a { draw(v:) { } }", "parameter type"},
        {"asset a { draw(v: blob) { } }", "parameter type"},
        {"asset a { draw(v: i32, a: i32) -> { } }", "parameter type"},
        {"asset a { f(x f32) { } }", "':' and a parameter type"},
    };
    for (const Case& testCase : cases) {
        INFO(testCase.source);
        bool threw = false;
        try {
            (void)parse(testCase.source);
        } catch (const SourceError& error) {
            threw = true;
            REQUIRE(std::string(error.message).find(testCase.messagePart) !=
                    std::string::npos);
        }
        REQUIRE(threw);
    }
}

TEST_CASE("palette grammar errors are located") {
    struct Case {
        const char* source;
        const char* messagePart;
    };
    const Case cases[] = {
        {"asset a { size 8x8 palette = [#ffffff] }", "a palette name"},
        {"asset a { size 8x8 palette p #ffffff }", "'='"},
        {"asset a { size 8x8 palette p = #ffffff }", "'['"},
        {"asset a { size 8x8 palette p = [#ffffff }", "']'"},
        {"asset a { size 8x8 palette p = [#ffffff,", "unterminated palette"},
        {"asset a { size 8x8 palette p = [#ffffff, }", "an expression"},
        {"asset a { size 8x8 draw(v: i32, a: i32) { c = p[ } }", "an expression"},
        {"asset a { size 8x8 draw(v: i32, a: i32) { c = p[0 } }", "']'"},
    };
    for (const Case& testCase : cases) {
        INFO(testCase.source);
        bool threw = false;
        try {
            (void)parse(testCase.source);
        } catch (const SourceError& error) {
            threw = true;
            REQUIRE(std::string(error.message).find(testCase.messagePart) !=
                    std::string::npos);
        }
        REQUIRE(threw);
    }
}

TEST_CASE("size values out of range are rejected") {
    bool threw = false;
    try {
        (void)parse("asset big { size 99999999999x1 }");
    } catch (const SourceError& error) {
        threw = true;
        REQUIRE(std::string(error.message).find("width out of range") !=
                std::string::npos);
    }
    REQUIRE(threw);
}

TEST_CASE("an empty program is one located error") {
    bool threw = false;
    try {
        (void)parse("");
    } catch (const SourceError& error) {
        threw = true;
        REQUIRE(error.location.line == 1);
        REQUIRE(error.location.column == 1);
        REQUIRE(std::string(error.message).find("expected an 'asset' block") !=
                std::string::npos);
    }
    REQUIRE(threw);
}

TEST_CASE("malformed programs produce one located error") {
    struct Case {
        const char* source;
        const char* messagePart;
    };
    const Case cases[] = {
        {"asset { }", "expected an asset name"},
        {"asset rock", "expected '{'"},
        {"asset rock { size }", "expected a width"},
        {"asset rock { size 192 abc }", "height"},
        {"asset rock { input color c }", "'='"},
        {"asset rock { input bogus c = 1 }", "input type"},
        {"asset rock { output blob c }", "output kind"},
        {"asset rock { draw v }", "'(' after the function name"},
        {"asset rock { draw(v: i32, a: i32) { w = } }", "expected an expression"},
        {"asset rock { draw(v: i32, a: i32) { 5 = 3 } }", "expected a statement"},
        {"asset rock { draw(v: i32, a: i32) { foo; } }", "must be a call"},
        {"asset rock { draw(v: i32, a: i32) { if x { } } }", "'(' after 'if'"},
        {"asset rock { draw(v: i32, a: i32) { if (x) 5 } }", "expected '{'"},
        {"asset rock { draw(v: i32, a: i32) {", "unterminated"},
        {"import hair from \"hair.ips\"", "expected an 'asset' block"},
        {"import hair from 5", "quoted file path"},
        {"asset a { } asset b { }", "multiple asset blocks"},
        {"asset rock { draw(v: i32, a: i32) { w = - } }", "expected an expression"},
        {"asset rock { draw(v: i32, a: i32) { w = {1, 2 } }", "unterminated"},
        {"asset rock { draw(v: i32, a: i32) { return 1 2 } }", "expected a statement"},
    };

    for (const Case& testCase : cases) {
        INFO(testCase.source);
        bool threw = false;
        try {
            (void)parse(testCase.source);
        } catch (const SourceError& error) {
            threw = true;
            REQUIRE(error.location.line == 1);
            REQUIRE(std::string(error.message).find(testCase.messagePart) !=
                    std::string::npos);
        }
        REQUIRE(threw);
    }
}

TEST_CASE("parse errors point at the right line and column") {
    const std::string source = "asset rock {\n  size 192x192\n  input color c = }\n}";
    try {
        (void)parse(source);
        FAIL("expected a SourceError");
    } catch (const SourceError& error) {
        // The offending token is the `}` on line 3, column 19.
        REQUIRE(error.location.line == 3);
        REQUIRE(error.location.column == 19);

        // The caret snippet lands under the token.
        const std::string formatted = formatError(source, error);
        REQUIRE(formatted.find("3:19: error:") != std::string::npos);
        REQUIRE(formatted.find("input color c = }") != std::string::npos);
        REQUIRE(formatted.find(std::string(18, ' ') + "^") != std::string::npos);
    }
}
