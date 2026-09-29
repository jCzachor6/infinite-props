#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "ips/Lexer.hpp"

// The .ips lexer: token streams of the language sketches, literal forms,
// comments, source locations and located errors.

using namespace iprop::ips;

namespace {

// The README §7.2 rock sketch (without the body's exact content).
const std::string kRockSketch = R"(asset rock {
  size 192x192
  origin 96,170
  variants 5
  archetypes 4
  input  color palette = #808d74
  output rect  collider

  draw(v, a) {
    w = range(21, 31)
    h = range(35, 49)
    collider = {96 - 20, 170 - 23, 40, 23}
    stone_mass(-w * 0.10, 0, w * 0.87, h, palette, seed)
    if (index(3) == 0) {
      stone_mass(-w * 0.67, 2, w * 0.39, h * 0.43,
                 shade(palette, -4), hash(seed ^ 9467))
    }
  }
})";

// The README §7.2 hair/portrait sketches.
const std::string kPortraitSketch = R"(
import hair from "hair.ips"
import eyes from "eyes.ips"

asset portrait {
  size 64x64
  variants 8
  input  color skin = #d9b58c
  input  color hairColor = #33291f
  output point pupilCenter

  draw(v, a) {
    face(v, a)
    hair.draw(seed ^ 0x9e3779b9, v, a, at = {32, 4}, color = hairColor)
    eyes.draw(seed ^ 0x517cc1b7, v, a, at = {32, 24})
    pupilCenter = eyes.outputs.pupil   // a child output surfaces in the parent
  }
})";

} // namespace

TEST_CASE("the rock sketch tokenizes end to end") {
    const auto tokens = lex(kRockSketch);
    REQUIRE(tokens.back().kind == TokenKind::End);

    // Spot-check the declaration section.
    const auto kinds = [&]() {
        std::vector<TokenKind> result;
        for (const auto& token : tokens) {
            result.push_back(token.kind);
        }
        return result;
    }();
    const auto find = [&](TokenKind kind) {
        for (const auto& token : tokens) {
            if (token.kind == kind) {
                return &token;
            }
        }
        return static_cast<const Token*>(nullptr);
    };

    REQUIRE(find(TokenKind::KeywordAsset) != nullptr);
    REQUIRE(find(TokenKind::KeywordInput) != nullptr);
    REQUIRE(find(TokenKind::KeywordOutput) != nullptr);
    REQUIRE(find(TokenKind::KeywordDraw) != nullptr);
    REQUIRE(find(TokenKind::KeywordIf) != nullptr);
    REQUIRE(find(TokenKind::TypeColor) != nullptr);
    REQUIRE(find(TokenKind::KindRect) != nullptr);

    // The colour literal keeps its exact text.
    const Token* color = find(TokenKind::Color);
    REQUIRE(color != nullptr);
    REQUIRE(color->text == "#808d74");

    // The float literal 0.10 survives verbatim.
    bool sawFloat = false;
    for (const auto& token : tokens) {
        if (token.kind == TokenKind::Float && token.text == "0.10") {
            sawFloat = true;
        }
    }
    REQUIRE(sawFloat);

}

TEST_CASE("the portrait sketch tokenizes imports, named args and comments") {
    const auto tokens = lex(kPortraitSketch);
    REQUIRE(tokens.back().kind == TokenKind::End);

    int imports = 0;
    int strings = 0;
    int namedArgs = 0;
    for (const auto& token : tokens) {
        if (token.kind == TokenKind::KeywordImport) {
            ++imports;
        }
        if (token.kind == TokenKind::String) {
            ++strings;
        }
        if (token.kind == TokenKind::KeywordAt) {
            ++namedArgs;
        }
    }
    REQUIRE(imports == 2);
    REQUIRE(strings == 2); // "hair.ips", "eyes.ips"
    REQUIRE(namedArgs == 2);

    // The hex seed literal.
    bool sawHex = false;
    for (const auto& token : tokens) {
        if (token.kind == TokenKind::Int && token.text == "0x9e3779b9") {
            sawHex = true;
        }
    }
    REQUIRE(sawHex);

    // The // comment must be skipped; "surfaces" is not a token.
    for (const auto& token : tokens) {
        REQUIRE(token.text != "surfaces");
    }
}

TEST_CASE("numeric literal forms") {
    const auto lexOne = [](const std::string& source) {
        const auto tokens = lex(source);
        REQUIRE(tokens.size() == 2); // literal + End
        return tokens[0];
    };
    REQUIRE(lexOne("42").kind == TokenKind::Int);
    REQUIRE(lexOne("0x9e3779b9").kind == TokenKind::Int);
    REQUIRE(lexOne("3.14").kind == TokenKind::Float);
    REQUIRE(lexOne("2.5e3").kind == TokenKind::Float);
    REQUIRE(lexOne("1e-3").kind == TokenKind::Float);

    // A leading dot is a Dot token followed by an integer.
    const auto dotted = lex(".5");
    REQUIRE(dotted.size() == 3);
    REQUIRE(dotted[0].kind == TokenKind::Dot);
    REQUIRE(dotted[1].kind == TokenKind::Int);
}

TEST_CASE("string literals and escapes") {
    const auto tokens = lex(R"("hair.ips" "a\tb" "quote:\" end")");
    REQUIRE(tokens.size() == 4); // 3 strings + End
    REQUIRE(tokens[0].text == "hair.ips");
    REQUIRE(tokens[1].text == "a\tb");
    REQUIRE(tokens[2].text == "quote:\" end");
}

TEST_CASE("comments and whitespace are skipped deterministically") {
    const std::string source = "  /* block\n comment */ a // line comment\n  b";
    const auto tokens = lex(source);
    REQUIRE(tokens.size() == 3); // a, b, End
    REQUIRE(tokens[0].kind == TokenKind::Identifier);
    REQUIRE(tokens[0].text == "a");
    REQUIRE(tokens[1].kind == TokenKind::Identifier);
    REQUIRE(tokens[1].text == "b");
    // 'a' is on line 2, column 13 (after the comment's "*/ ").
    REQUIRE(tokens[0].location.line == 2);
    REQUIRE(tokens[0].location.column == 13);
    // 'b' is on line 3, column 3.
    REQUIRE(tokens[1].location.line == 3);
    REQUIRE(tokens[1].location.column == 3);
}

TEST_CASE("lexical errors carry locations") {
    const auto expectError = [](const std::string& source, std::size_t line,
                                std::size_t column, const char* messagePart) {
        bool threw = false;
        try {
            (void)lex(source);
        } catch (const LexError& error) {
            threw = true;
            REQUIRE(error.location.line == line);
            REQUIRE(error.location.column == column);
            REQUIRE(std::string(error.message).find(messagePart) != std::string::npos);
        }
        REQUIRE(threw);
    };

    expectError("\"unterminated", 1, 1, "unterminated string");
    expectError("/* never closed", 1, 1, "unterminated block comment");
    expectError("0x", 1, 1, "hexadecimal");
    expectError("#123", 1, 1, "colour");
    expectError("#12345", 1, 1, "colour");
    expectError("@", 1, 1, "unexpected character");
    expectError("\"bad\\q\"", 1, 1, "escape");
}

TEST_CASE("formatError renders a caret snippet") {
    const std::string source = "asset rock {\n  bogus @ token\n}";
    try {
        (void)lex(source);
        FAIL("expected a LexError");
    } catch (const LexError& error) {
        const std::string formatted = formatError(source, error);
        REQUIRE(formatted.find("2:9: error:") != std::string::npos);
        REQUIRE(formatted.find("bogus @ token") != std::string::npos);
        // The caret line: 8 spaces then '^'.
        REQUIRE(formatted.find("        ^") != std::string::npos);
    }
}

TEST_CASE("operators lex with correct arity") {
    const auto tokens = lex("== != <= >= && || -> + - * / % ^ & | ~ ! = < >");
    const std::vector<TokenKind> expected = {
        TokenKind::Equal,    TokenKind::NotEqual, TokenKind::LessEqual,
        TokenKind::GreaterEqual, TokenKind::AndAnd, TokenKind::OrOr,
        TokenKind::Arrow,    TokenKind::Plus,     TokenKind::Minus,
        TokenKind::Star,     TokenKind::Slash,    TokenKind::Percent,
        TokenKind::Caret,    TokenKind::Amp,      TokenKind::Pipe,
        TokenKind::Tilde,    TokenKind::Bang,     TokenKind::Assign,
        TokenKind::Less,     TokenKind::Greater,
    };
    REQUIRE(tokens.size() == expected.size() + 1); // + End
    for (std::size_t i = 0; i < expected.size(); ++i) {
        INFO("operator " << i);
        REQUIRE(tokens[i].kind == expected[i]);
    }
}
