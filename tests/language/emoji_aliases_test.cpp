#include <catch2/catch_test_macros.hpp>

#include "Compiler.hpp"
#include "iprop/Debug.hpp"
#include "ips/Lexer.hpp"
#include "ips/Parser.hpp"
#include "ips/Printer.hpp"
#include "ips/Semantics.hpp"
#include "support/compiler_support.hpp"

using namespace iprop;

namespace {
const std::pair<const char*, const char*> aliases[] = {
        {"📦", "asset"}, {"✏", "draw"}, {"📥", "input"}, {"📤", "output"},
        {"🌈", "palette"}, {"🔗", "import"}, {"🎨", "color"}, {"📐", "vec2"},
        {"🔀", "variants"}, {"🗂", "archetypes"}, {"🔘", "point"}, {"🟦", "rect"},
        {"⭕", "circle"}, {"👍", "true"}, {"👎", "false"}, {"🌀", "noise"},
        {"📏", "dist"}, {"🦠", "cell"}
};
}

TEST_CASE("the exact 18 emoji aliases normalize to canonical word tokens") {
    for (const auto& [emoji, word] : aliases) {
        INFO(word);
        const auto actual = ips::lex(std::string(emoji) + " next");
        const auto expected = ips::lex(word);
        REQUIRE(actual.size() == 3);
        CHECK(actual[0].kind == expected[0].kind);
        CHECK(actual[0].text == word);
        CHECK(actual[1].location.column == 3);
        // A selector or joiner is not part of any alias, even when rendered
        // as the same glyph by a font. Comments and strings remain literal.
        for (const auto suffix : {"\xEF\xB8\x8F", "\xEF\xB8\x8E", "\xE2\x80\x8D"})
            CHECK_THROWS_AS(ips::lex(std::string(emoji) + suffix), ips::SourceError);
    }
    CHECK(ips::lex("\"✏️\" /* 🗂️ */")[0].text == "✏️");
    CHECK_THROWS_AS(ips::lex("dist.⭕"), ips::SourceError);
    CHECK_THROWS_AS(ips::lex("🌟"), ips::SourceError);
}

TEST_CASE("emoji source normalizes AST diagnostics and executable bytes") {
    const std::string source = R"(🔗 tile from "tile.ips"
      📦 aliases { size 8x8 🔀 2 🗂 3
        📥 🎨 tint = #fff
        📤 🔘 p 📤 🟦 r 📤 ⭕ c
        🌈 colors = [#f00, #0f0]
        ✏ {
          p = {v,a} r={0,0,2,2} c={1,1,1}
          if (👍 && !👎) { 🟦(0,0,4,4,tint) }
          q = helper({1,2}, colors[v])
          d = 📏.circle(q, {0,0}, 2)
          n = 🌀.fbm(0.25, 0.5, 2, seed)
          j = 🦠.jitter(0,0,seed)
          pixel(4,0,🎨.shade(#fff,int((d+n+j.x)*10)))
          tile.draw(seed,v,a,at={4,4})
        }
        helper(p:📐, c:🎨)->📐 { pixel(0,4,c) return p }
      })";
    const auto program = ips::parse(source);
    auto words = source;
    for (const auto& [emoji, canonical] : aliases) {
        std::size_t at = 0;
        while ((at = words.find(emoji, at)) != std::string::npos) {
            words.replace(at, std::char_traits<char>::length(emoji), canonical);
            at += std::char_traits<char>::length(canonical);
        }
    }
    const auto wordProgram = ips::parse(words);
    const auto text = ips::formatProgram(program);
    const auto word = ips::parse(text);
    const auto child = ips::parse("asset tile { size 4x4 draw { pixel(0,0,#f00) } }");
    const std::map<std::string, const ips::Program*> parts{{"tile", &child}};
    ips::checkAsset(program, {{"tile", {}}});
    CHECK(ips::printProgram(program) == ips::printProgram(wordProgram));
    CHECK(compileProgram(program, parts) == compileProgram(wordProgram, parts));
    CHECK(ips::formatProgram(word) == text);
    // Formatting changes line positions; stripped executable bytes are exact.
    CHECK(stripDebug(compileProgram(program, parts)) == stripDebug(compileProgram(word, parts)));
    CHECK(text.find("📦") == std::string::npos);
    try { (void)compiler_test::compileChecked("📦 t { size 8x8 ✏ { 🎨.shadde(#fff, 1) } }"); FAIL("expected diagnostic"); }
    catch (const ips::SourceError& error) {
        CHECK(error.message.find("color.shadde") != std::string::npos);
        CHECK(error.message.find("color.shade") != std::string::npos);
        CHECK(error.message.find("🎨") == std::string::npos);
    }
}
