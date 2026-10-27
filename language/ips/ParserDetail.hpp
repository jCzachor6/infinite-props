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
#include "ips/Parser.hpp"
#include "ips/Suggestions.hpp"

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

class ParserImpl {
public:
    explicit ParserImpl(std::vector<Token> tokens) : tokens_(std::move(tokens)) {}

    ParseResult parseRecoveringProgram(std::size_t limit);

    // ---- token helpers (shared) ------------------------------------------

    // The lexer always ends the stream with TokenKind::End.
    const Token& peek(std::size_t ahead = 0) const {
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
            failExpected(what, tokenKind);
        }
        return advance();
    }

    const Token& expectName(const char* what) {
        if (!isNameKind(kind())) {
            failExpected(what);
        }
        return advance();
    }

    [[noreturn]] void failExpected(const char* what, std::optional<TokenKind> expected = {}) {
        std::string message = "expected ";
        message += what;
        message += ", got ";
        message += tokenName(kind());
        if (kind() == TokenKind::Identifier) {
            if (expected) {
                const std::vector<std::string> names{tokenName(*expected)};
                message += didYouMean(peek().text, names);
            } else message += keywordSuggestion(peek().text);
        }
        throw SourceError{peek().location, std::move(message), {},
                          DiagnosticCode::ExpectedToken, DiagnosticStage::Parser};
    }

    [[noreturn]] void failHere(std::string message) {
        throw SourceError{peek().location, std::move(message), {},
                          DiagnosticCode::InvalidSyntax, DiagnosticStage::Parser};
    }

    // ---- program, declarations, statements (Parser.cpp) --------------------

    Import parseImport();
    MaterialDef parseMaterialDef();
    Asset parseAsset();
    void parseDeclaration(Asset& asset);
    void parseSize(Asset& asset);
    void parseOrigin(Asset& asset);
    InputDecl parseInput();
    std::unique_ptr<Expr> parseLiteralValue(bool allowMinus = true);
    OutputDecl parseOutput();
    PaletteDecl parsePalette();
    Function parseFunction();
    Function parseFunctionAfterName(const Token& name);
    ParamType parseParamType(bool insideList = false);
    std::vector<Stmt> parseBlock();
    Stmt parseStatement();
    Stmt parseIdentifierStatement();
    Stmt parseLayer();
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
    std::unique_ptr<Expr> parseRandomRange();
    std::unique_ptr<Expr> parseShift();
    std::unique_ptr<Expr> parseAdditive();
    std::unique_ptr<Expr> parseMultiplicative();
    std::unique_ptr<Expr> parseUnary();
    std::unique_ptr<Expr> parsePostfix();
    std::unique_ptr<Expr> parsePostfixAfter(std::unique_ptr<Expr> expr);
    std::unique_ptr<Expr> parsePrimary();
    std::unique_ptr<Expr> parseLiteral();
    std::unique_ptr<Expr> parseVector();
    std::unique_ptr<Expr> parseList();
    std::unique_ptr<Expr> parsePointDistribution();
    std::vector<Arg> parseArgs();

private:
    enum class RecoveryScope { Program, Declaration, Statement };
    struct RecoveryHalt {};
    void recordError(SourceError error);
    void synchronize(std::size_t start, RecoveryScope scope);
    bool recoveryStart(RecoveryScope scope) const;

    std::vector<Token> tokens_;
    std::size_t pos_ = 0;
    bool listTypeClosed_ = false;
    ParseResult* recovery_ = nullptr;
    std::size_t diagnosticLimit_ = kMaxParseDiagnostics;
};

} // namespace detail
} // namespace iprop::ips
