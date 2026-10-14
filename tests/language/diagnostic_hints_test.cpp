#include <catch2/catch_test_macros.hpp>

#include "ips/Lexer.hpp"
#include "ips/Parser.hpp"
#include "ips/SourceText.hpp"
#include "ips/Suggestions.hpp"
#include "support/semantic_support.hpp"

using namespace iprop::ips;

TEST_CASE("diagnostics suggest canonical functions variables parameters and palettes") {
    using namespace semantic_test;
    expectCheckError(wrap("smooh(0.5)"), "did you mean 'smooth'");
    expectCheckError(wrap("mni(1, 2)"), "did you mean 'min'");
    expectCheckError(wrap("color.shadde(#fff, 1)"), "did you mean 'color.shade'");
    expectCheckError(wrap("coloor.mix(#fff, #000, 0.5)"), "did you mean 'color.mix'");
    expectCheckError(wrap("shade(#fff, 1)"), "did you mean 'color.shade'");
    expectCheckError(wrap("width = 2 result = widht"), "did you mean 'width'");
    expectCheckError(wrap("rect(x=0, y=0, w=4, h=4, colro=#fff)"), "did you mean 'color'");
    expectCheckError("asset t { size 8x8 palette colors=[#fff] draw { ramp(colros, 0.5) } }", "did you mean 'colors'");
    expectCheckError("asset t { size 8x8 draw { hepler(1) } helper(x:i32) { } }", "did you mean 'helper'");
    const std::vector<std::string> choices{"noise", "shade", "smooth"};
    CHECK(didYouMean("someCompletelyDifferentName", choices).empty());
    CHECK(didYouMean("smooth", choices).empty());
    CHECK(didYouMean("shdae", choices) == "; did you mean 'shade'?");
}

TEST_CASE("parser hints match expected keywords and misplaced declarations") {
    const auto message = [](const std::string& source) {
        try { (void)parse(source); } catch (const SourceError& error) { return error.message; }
        FAIL("expected a diagnostic");
        return std::string{};
    };
    CHECK(message("assset t { size 8x8 draw { } }").find("did you mean 'asset'") != std::string::npos);
    CHECK(message("asset t { size 8x8 orgin 0,0 draw { } }").find("did you mean 'origin'") != std::string::npos);
    CHECK(message("asset t { size 8x8 draw { retrun } }").find("did you mean 'return'") != std::string::npos);
    CHECK(message("import tile form \"tile.ips\" asset t { }").find("did you mean 'from'") != std::string::npos);
}

TEST_CASE("source locations count UTF-8 codepoints in comments strings and CRLF") {
    const auto comments = lex("/* ż📦 */\tasset t { }\r\nend");
    CHECK(comments[0].location.column == 10);
    CHECK(comments[4].location.line == 2);
    CHECK(comments[4].location.column == 1);
    const auto strings = lex("\"ż📦€\" tail");
    CHECK(strings[1].location.column == 7);
    const auto multiline = lex("\"ż\n📦\" next");
    CHECK(multiline[1].location.line == 2);
    CHECK(multiline[1].location.column == 4);
    const std::string source = "\t/* ż📦 */ @";
    try { (void)lex(source); FAIL("expected error"); }
    catch (const SourceError& error) {
        CHECK(error.location.column == 11);
        CHECK(formatError(source, error).find("\n\t         ^\n") != std::string::npos);
    }
    try { (void)lex("/* 📦 */ 🌟"); FAIL("expected unsupported glyph"); }
    catch (const SourceError& error) {
        CHECK(error.location.column == 9);
        CHECK(error.message == "unexpected character '🌟'");
    }
}

TEST_CASE("source decoder rejects malformed UTF-8 at the offending codepoint") {
    for (const std::string invalid : {"\x80", "\xFF", "\xC0\xAF", "\xE0\x80\xAF", "\xF0\x80\x80\xAF",
            "\xED\xA0\x80", "\xED\xBF\xBF", "\xF4\x90\x80\x80", "\xC2", "\xE2\x82", "\xF0\x9F\x9A",
            "\xC2\x41", "\xE2\x82\x41", "\xF0\x9F\x9A\x41"}) {
        CHECK_FALSE(decodeCodepoint(invalid, 0));
        try { (void)lex("/* ż📦 " + invalid + " */"); FAIL("expected invalid UTF-8"); }
        catch (const SourceError& error) {
            CHECK(error.location.column == 7);
            CHECK(error.message == "invalid UTF-8 in source");
        }
    }
    REQUIRE(decodeCodepoint("📦", 0));
    CHECK(decodeCodepoint("📦", 0)->value == 0x1F4E6);
    CHECK(decodeCodepoint("📦", 0)->bytes == 4);
    CHECK(decodeCodepoint("\xF4\x8F\xBF\xBF", 0)->value == 0x10FFFF);
}
