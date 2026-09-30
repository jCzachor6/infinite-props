#pragma once

// The .ips lexer: turns source text into a token stream with precise
// source locations. Purely mechanical — no grammar knowledge beyond the
// lexical level.

#include <string>
#include <string_view>
#include <vector>

#include "ips/SourceError.hpp"

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
    KeywordBreak,
    KeywordContinue,
    KeywordFor,
    KeywordIn,
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
    DotDot, // ..
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
    Shl,     // <<
    Shr,     // >>

    Tilde,   // ~
    Bang,    // !
    AndAnd,  // &&
    OrOr,    // ||
};

// 1-based line/column lives in SourceError.hpp (included above).

struct Token {
    TokenKind kind = TokenKind::End;
    std::string text; // the exact lexeme (literals and identifiers)
    SourceLocation location;
};

// Lexes the whole source into a token vector ending with TokenKind::End.
// Throws SourceError on malformed input.
std::vector<Token> lex(std::string_view source);

// Descriptive name of a token kind (diagnostics and tests).
const char* tokenName(TokenKind kind);

} // namespace iprop::ips
