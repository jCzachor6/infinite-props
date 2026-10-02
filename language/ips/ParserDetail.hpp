#pragma once

// Internal implementation of the .ips parser, shared between Parser.cpp
// (program, declarations and statements) and ParserExpr.cpp (expressions).
// Not part of the public front-end API.

#include <cstdint>
#include <cstdlib>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "ips/Ast.hpp"
#include "ips/Lexer.hpp"

namespace iprop::ips {
namespace detail {

// Converts a numeric token to a u32, rejecting values out of range.
inline std::uint32_t toU32(const Token& token, const char* what) {
    char* end = nullptr;
    const unsigned long long value = std::strtoull(token.text.c_str(), &end, 0);
    if (end == token.text.c_str() || *end != '\0' || value > 0xFFFFFFFFull) {
        throw SourceError{token.location,
                          std::string(what) + " out of range: " + token.text};
    }
    return static_cast<std::uint32_t>(value);
}

inline bool isDigits(std::string_view text) {
    if (text.empty()) {
        return false;
    }
    for (const char c : text) {
        if (c < '0' || c > '9') {
            return false;
        }
    }
    return true;
}

// "Soft" keywords may appear wherever a name is expected: `hair.draw(...)`,
// `at = {32, 4}`, `input color color = #33291f`. Hard keywords (control
// flow and declaration introducers) stay reserved.
inline bool isNameKind(TokenKind tokenKind) {
    switch (tokenKind) {
    case TokenKind::Identifier:
    case TokenKind::KeywordDraw:
    case TokenKind::KeywordAt:
    case TokenKind::TypeColor:
    case TokenKind::TypeF32:
    case TokenKind::TypeU32:
    case TokenKind::TypeI32:
    case TokenKind::TypeVec2:
    case TokenKind::KindPoint:
    case TokenKind::KindRect:
    case TokenKind::KindCircle:
        return true;
    default:
        return false;
    }
}

class ParserImpl {
public:
    explicit ParserImpl(std::vector<Token> tokens) : tokens_(std::move(tokens)) {}

    Program parseProgram();

    // ---- token helpers (shared) ------------------------------------------

    const Token& peek(std::size_t ahead = 0) const {
        if (tokens_.empty()) {
            static const Token kEnd{};
            return kEnd;
        }
        const std::size_t at = pos_ + ahead;
        return at < tokens_.size() ? tokens_[at] : tokens_.back();
    }

    TokenKind kind(std::size_t ahead = 0) const { return peek(ahead).kind; }

    const Token& advance() {
        return pos_ < tokens_.size() ? tokens_[pos_++] : tokens_.back();
    }

    bool match(TokenKind tokenKind) {
        if (kind() != tokenKind) {
            return false;
        }
        ++pos_;
        return true;
    }

    const Token& expect(TokenKind tokenKind, const char* what) {
        if (kind() != tokenKind) {
            failExpected(what);
        }
        return advance();
    }

    const Token& expectName(const char* what) {
        if (!isNameKind(kind())) {
            failExpected(what);
        }
        return advance();
    }

    [[noreturn]] void failExpected(const char* what) {
        std::string message = "expected ";
        message += what;
        message += ", got ";
        message += tokenName(kind());
        failHere(std::move(message));
    }

    [[noreturn]] void failHere(std::string message) {
        throw SourceError{peek().location, std::move(message)};
    }

    // ---- program, declarations, statements (Parser.cpp) --------------------

    Import parseImport();
    Asset parseAsset();
    void parseSize(Asset& asset);
    void parseOrigin(Asset& asset);
    InputDecl parseInput();
    std::unique_ptr<Expr> parseLiteralValue();
    OutputDecl parseOutput();
    PaletteDecl parsePalette();
    Function parseFunction();
    Function parseFunctionAfterName(const Token& name);
    ParamType parseParamType();
    std::vector<Stmt> parseBlock();
    Stmt parseStatement();
    Stmt parseIdentifierStatement();
    Stmt parseIf();
    Stmt parseWhile();
    Stmt parseFor();
    Stmt parseReturn();

    // ---- expressions (ParserExpr.cpp) -------------------------------------

    std::unique_ptr<Expr> parseExpr();
    std::unique_ptr<Expr> parseOr();
    std::unique_ptr<Expr> parseAnd();
    std::unique_ptr<Expr> parseBitOr();
    std::unique_ptr<Expr> parseBitXor();
    std::unique_ptr<Expr> parseBitAnd();
    std::unique_ptr<Expr> parseEquality();
    std::unique_ptr<Expr> parseComparison();
    std::unique_ptr<Expr> parseShift();
    std::unique_ptr<Expr> parseAdditive();
    std::unique_ptr<Expr> parseMultiplicative();
    std::unique_ptr<Expr> parseUnary();
    std::unique_ptr<Expr> parsePostfix();
    std::unique_ptr<Expr> parsePostfixAfter(std::unique_ptr<Expr> expr);
    std::unique_ptr<Expr> parsePrimary();
    std::unique_ptr<Expr> parseLiteral();
    std::unique_ptr<Expr> parseVector();
    std::vector<Arg> parseArgs();

private:
    std::vector<Token> tokens_;
    std::size_t pos_ = 0;
};

} // namespace detail
} // namespace iprop::ips
