#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>

#include "ips/Parser.hpp"
#include "ips/Printer.hpp"

using namespace iprop::ips;

TEST_CASE("parser recovery preserves complete programs and strict first errors", "[parser-recovery]") {
    const std::string source = R"(
import part from "part.ips";
asset a { size 8x8; input i32 offset = -2;
    helper(x: i32) -> i32 { return x + 1; }
    draw { if (true) { helper(offset); } else { helper(3); } }
})";
    auto recovered = parseRecovering(source);
    REQUIRE(recovered.valid());
    REQUIRE(recovered.program.has_value());
    CHECK(recovered.diagnostics.empty());
    CHECK_FALSE(recovered.limitReached);
    REQUIRE(recovered.imports.size() == 1);
    CHECK(recovered.imports.front().alias == "part");
    CHECK(formatProgram(*recovered.program) == formatProgram(parse(source)));
    CHECK(parseDiagnostics(source).empty());

    const std::string invalid = "asset a { draw { x = ; y = ; } }";
    auto diagnostics = parseDiagnostics(invalid);
    REQUIRE(diagnostics.size() == 2);
    CHECK_FALSE(parseRecovering(invalid).program.has_value());
    try {
        (void)parse(invalid);
        FAIL("strict parse must still throw the first syntax error");
    } catch (const SourceError& strict) {
        CHECK(diagnostics.front().message == strict.message);
        CHECK(diagnostics.front().location.byteOffset == strict.location.byteOffset);
        CHECK(diagnostics.front().code == strict.code);
        CHECK(diagnostics.front().stage == strict.stage);
    }
}

TEST_CASE("parser recovery collects independent imports declarations and statements", "[parser-recovery]") {
    const std::string source = R"(
import broken from 123;
import good from "good.ips";
import alsoBroken 99;
import second from "second.ips";
asset a {
    input blob wrong = 0;
    output blob invalid;
    size 8x8;
    draw { x = ; pixel(,); y = 1; z = ; }
})";
    const auto result = parseRecovering(source);
    CHECK_FALSE(result.valid());
    CHECK_FALSE(result.program.has_value());
    CHECK_FALSE(result.limitReached);
    REQUIRE(result.diagnostics.size() == 7);
    REQUIRE(result.imports.size() == 2);
    CHECK(result.imports[0].alias == "good");
    CHECK(result.imports[0].path == "good.ips");
    CHECK(result.imports[1].alias == "second");
    for (std::size_t i = 0; i < result.diagnostics.size(); ++i) {
        CHECK(result.diagnostics[i].stage == DiagnosticStage::Parser);
        CHECK(result.diagnostics[i].code != DiagnosticCode::Unspecified);
        if (i > 0) CHECK(result.diagnostics[i].location.byteOffset >
                         result.diagnostics[i - 1].location.byteOffset);
    }
}

TEST_CASE("parser recovery retains nested block ownership", "[parser-recovery]") {
    const auto result = parseRecovering(R"(
asset a {
    broken(x:) { inner = ; if (true) { hidden = ; } }
    input i32 valid = -2;
    draw {
        if (true) {
            x = ;
            while (true) { y = ; break; }
            good(1);
        } else { z = ; }
        outside = ;
    }
    output blob wrong;
})");
    REQUIRE(result.diagnostics.size() == 6);
    CHECK(result.diagnostics[0].message.find("parameter type") != std::string::npos);
    CHECK(result.diagnostics.back().message.find("output kind") != std::string::npos);
    CHECK_FALSE(result.valid());
    CHECK_FALSE(result.limitReached);
}

TEST_CASE("parser recovery skips malformed conditional arms as one production", "[parser-recovery]") {
    const auto result = parseRecovering(R"(
asset a { draw {
    if bad { hidden = ; } else if (true) { alsoHidden = ; } else { hiddenAgain = ; }
    outside = ;
} })");
    REQUIRE(result.diagnostics.size() == 2);
    CHECK(result.diagnostics[0].message.find("'(' after 'if'") != std::string::npos);
    CHECK(result.diagnostics[1].message.find("an expression") != std::string::npos);
}

TEST_CASE("parser recovery uses semicolons tokens and braces rather than newlines", "[parser-recovery]") {
    const auto multiline = parseRecovering(R"(
asset a { draw {
    pixel(0,
        ,
        named = 2,
        nested(3));
    x = ;
} })");
    REQUIRE(multiline.diagnostics.size() == 2);
    CHECK(multiline.diagnostics[0].location.line == 4);
    CHECK(multiline.diagnostics[1].location.line == 7);

    // Statement/declaration introducers at delimiter depth zero also work
    // without semicolons; no source line is consulted by synchronization.
    const auto tokens = parseRecovering(
        "asset a { input blob bad = 0 output circle ok draw { x = if (true) { y = ; } z = ; } }");
    REQUIRE(tokens.diagnostics.size() == 4);
    const auto missingClose = parseRecovering(
        "asset a { draw { broken(1, ; x = ; } output blob bad; }");
    REQUIRE(missingClose.diagnostics.size() == 3);
}

TEST_CASE("parser recovery caps repeated errors with bounded caller limits", "[parser-recovery]") {
    std::string source = "import p from \"p.ips\"; asset a { draw {";
    for (int i = 0; i < 80; ++i) source += "x = ;";
    source += "} }";
    struct Case { std::size_t requested; std::size_t count; };
    for (const auto test : {Case{0, 1}, Case{1, 1}, Case{3, 3}, Case{20, 20}, Case{999, 20}}) {
        const auto result = parseRecovering(source, test.requested);
        REQUIRE(result.diagnostics.size() == test.count);
        CHECK(result.limitReached);
        CHECK_FALSE(result.valid());
        REQUIRE(result.imports.size() == 1);
        CHECK(result.imports.front().alias == "p");
    }
    CHECK_FALSE(parseRecovering("asset a {}", 0).limitReached);
}

TEST_CASE("parser recovery reports EOF once without nested cascades", "[parser-recovery]") {
    for (const auto source : {
             "", "import p from \"p.ips\";", "import p from",
             "asset a", "asset a { draw { if (true) { x =",
             "asset a { palette p = [1,", "asset a { draw { f(1,"}) {
        INFO(source);
        const auto result = parseRecovering(source);
        REQUIRE(result.diagnostics.size() == 1);
        CHECK_FALSE(result.valid());
        CHECK_FALSE(result.limitReached);
    }
    const auto result = parseRecovering("asset a { draw { x = ; if (true) { y =");
    REQUIRE(result.diagnostics.size() == 2);
    CHECK(result.diagnostics.back().message.find("an expression") != std::string::npos);
}

TEST_CASE("parser recovery converts lexical failures into one typed diagnostic", "[parser-recovery]") {
    for (const auto& source : {std::string("asset a { draw { @ } }"),
                              std::string("import p from \"unterminated"),
                              std::string("asset a {}") + char(0xff)}) {
        const auto result = parseRecovering(source);
        REQUIRE(result.diagnostics.size() == 1);
        CHECK_FALSE(result.valid());
        CHECK(result.imports.empty());
        CHECK(result.diagnostics.front().stage == DiagnosticStage::Lexer);
        CHECK(result.diagnostics.front().code != DiagnosticCode::Unspecified);
    }
}

TEST_CASE("parser recovery normalizes nonterminated token streams and makes progress", "[parser-recovery]") {
    auto tokens = lex("asset a { draw { x = ; y = ; } }");
    tokens.pop_back();
    CHECK(parseTokensRecovering(tokens).diagnostics.size() == 2);
    CHECK(parseTokensRecovering({}).diagnostics.size() == 1);
    auto validTokens = lex("import p from \"p.ips\"; asset a {}");
    validTokens.pop_back();
    CHECK(parseTokensRecovering(validTokens).valid());

    // Many tokens at a failing position (including unmatched closing braces)
    // must terminate either at EOF or at the bounded diagnostic cap.
    for (const auto source : {
             "} } } import good from \"good.ips\"; asset a {}",
             "asset a { draw { ] ] ] ; x = ; } }",
             "asset broken { draw( { input blob hidden = ; } } asset a { output blob bad; }",
             "asset a { draw { x = {1, ; y = ; } z = ; } }"}) {
        INFO(source);
        const auto result = parseRecovering(source);
        CHECK_FALSE(result.valid());
        CHECK_FALSE(result.diagnostics.empty());
        CHECK(result.diagnostics.size() <= kMaxParseDiagnostics);
    }
}

TEST_CASE("parser recovery retains only grammatical imports for discovery", "[parser-recovery]") {
    const auto result = parseRecovering(R"(
import broken from 99;
import complete from "complete.ips";
asset a { draw { import hidden from "hidden.ips"; x = ; } }
import trailing from "trailing.ips";
)");
    REQUIRE(result.imports.size() == 1);
    CHECK(result.imports.front().alias == "complete");
    CHECK_FALSE(result.program.has_value());
    REQUIRE(result.diagnostics.size() == 4);
    const auto eof = parseRecovering("import complete from \"complete.ips\";");
    REQUIRE(eof.imports.size() == 1);
    CHECK(eof.diagnostics.size() == 1);
    CHECK_FALSE(eof.valid());
}

TEST_CASE("parser recovery terminates on a deterministic malformed token corpus", "[parser-recovery]") {
    const char* fragments[] = {"x", "=", ";", "(", ")", "[", "]", "{", "}",
                              ",", "if", "else", "while", "input", "import", "1"};
    std::uint32_t state = 0x4271;
    for (int sample = 0; sample < 200; ++sample) {
        std::string source = "import p from \"p.ips\"; asset a { draw { ";
        for (int token = 0; token < 100; ++token) {
            state = state * 1664525u + 1013904223u;
            source += fragments[(state >> 16) % 16];
            source += ' ';
        }
        source += "} }";
        const auto result = parseRecovering(source, 5);
        CHECK_FALSE(result.valid());
        CHECK_FALSE(result.diagnostics.empty());
        CHECK(result.diagnostics.size() <= 5);
        CHECK(result.imports.size() == 1);
    }
}
