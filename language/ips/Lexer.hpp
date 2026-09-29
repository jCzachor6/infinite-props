#pragma once

// The .ips lexer: turns source text into a token stream with precise
// source locations. Purely mechanical — no grammar knowledge beyond the
// lexical level.

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace iprop::ips {

enum class TokenKind {
    End, // end of input

    Identifier,

    // Keywords.
    KeywordAsset,
    KeywordInput,
    KeywordOutput,
    KeywordImport,
    KeywordFrom,
    KeywordDraw,
    KeywordAt,
    KeywordIf,
    KeywordElse,
    KeywordWhile,
    KeywordReturn,
    KeywordTrue,
    KeywordFalse,
    KeywordList,

    // Type names.
    TypeColor,
    TypeF32,
    TypeU32,
    TypeI32,

    // Output kinds.
    KindPoint,
    KindRect,
    KindCircle,

    // Literals (text holds the exact lexeme).
    Int,
    Float,
    Color,
    String,

    // Punctuation.
    LBrace,
    RBrace,
    LParen,
    RParen,
    LBracket,
    RBracket,
    Comma,
    Dot,
    Colon,
    Semi,
    Arrow, // ->

    // Operators.
    Assign,    // =
    Equal,     // ==
    NotEqual,  // !=
    Less,      // <
    LessEqual, // <=
    Greater,   // >
    GreaterEqual, // >=
    Plus,
    Minus,
    Star,
    Slash,
    Percent,
    Caret,   // ^
    Amp,     // &
    Pipe,    // |
    Tilde,   // ~
    Bang,    // !
    AndAnd,  // &&
    OrOr,    // ||
};

// 1-based line/column.
struct SourceLocation {
    std::size_t line = 1;
    std::size_t column = 1;
};

struct Token {
    TokenKind kind = TokenKind::End;
    std::string text; // the exact lexeme (literals and identifiers)
    SourceLocation location;
};

// A lexical error with a source location.
struct LexError {
    SourceLocation location;
    std::string message;
};

// Lexes the whole source into a token vector ending with TokenKind::End.
// Throws LexError on malformed input.
std::vector<Token> lex(std::string_view source);

// Descriptive name of a token kind (diagnostics and tests).
const char* tokenName(TokenKind kind);

// Formats "line:col: error: <message>" plus the offending source line and
// a caret pointing at the column.
std::string formatError(std::string_view source, const LexError& error);

} // namespace iprop::ips
