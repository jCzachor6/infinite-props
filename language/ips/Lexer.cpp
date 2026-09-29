#include "ips/Lexer.hpp"

#include <map>
#include <string>

namespace iprop::ips {
namespace {

const std::map<std::string, TokenKind>& keywords() {
    static const std::map<std::string, TokenKind> table = {
        {"asset", TokenKind::KeywordAsset},
        {"input", TokenKind::KeywordInput},
        {"output", TokenKind::KeywordOutput},
        {"import", TokenKind::KeywordImport},
        {"from", TokenKind::KeywordFrom},
        {"draw", TokenKind::KeywordDraw},
        {"at", TokenKind::KeywordAt},
        {"if", TokenKind::KeywordIf},
        {"else", TokenKind::KeywordElse},
        {"while", TokenKind::KeywordWhile},
        {"return", TokenKind::KeywordReturn},
        {"true", TokenKind::KeywordTrue},
        {"false", TokenKind::KeywordFalse},
        {"list", TokenKind::KeywordList},
        {"color", TokenKind::TypeColor},
        {"f32", TokenKind::TypeF32},
        {"u32", TokenKind::TypeU32},
        {"i32", TokenKind::TypeI32},
        {"point", TokenKind::KindPoint},
        {"rect", TokenKind::KindRect},
        {"circle", TokenKind::KindCircle},
    };
    return table;
}

bool isDigit(char c) { return c >= '0' && c <= '9'; }
bool isHexDigit(char c) {
    return isDigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}
bool isIdentStart(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}
bool isIdentPart(char c) { return isIdentStart(c) || isDigit(c); }

} // namespace

std::vector<Token> lex(std::string_view source) {
    std::vector<Token> tokens;
    std::size_t pos = 0;
    std::size_t line = 1;
    std::size_t column = 1;

    const auto location = [&]() { return SourceLocation{line, column}; };
    const auto error = [&](SourceLocation at, std::string message) -> SourceError {
        return SourceError{at, std::move(message)};
    };
    const auto advance = [&]() -> char {
        const char c = source[pos++];
        if (c == '\n') {
            ++line;
            column = 1;
        } else {
            ++column;
        }
        return c;
    };
    // peek(ahead) looks at source[pos + ahead]; the default (ahead = 1) is
    // the NEXT character. Current-character reads use peek(0).
    const auto peek = [&](std::size_t ahead = 1) -> char {
        return pos + ahead < source.size() ? source[pos + ahead] : '\0';
    };
    const auto emit = [&](TokenKind kind, std::string text, SourceLocation at) {
        tokens.push_back({kind, std::move(text), at});
    };

    while (pos < source.size()) {
        const SourceLocation at = location();
        const char c = source[pos];

        // Whitespace.
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            advance();
            continue;
        }

        // Line comment.
        if (c == '/' && peek() == '/') {
            while (pos < source.size() && source[pos] != '\n') {
                advance();
            }
            continue;
        }
        // Block comment.
        if (c == '/' && peek() == '*') {
            advance();
            advance();
            bool closed = false;
            while (pos < source.size()) {
                if (source[pos] == '*' && peek() == '/') {
                    advance();
                    advance();
                    closed = true;
                    break;
                }
                advance();
            }
            if (!closed) {
                throw error(at, "unterminated block comment");
            }
            continue;
        }

        // String literal.
        if (c == '"') {
            advance();
            std::string text;
            bool closed = false;
            while (pos < source.size()) {
                const char s = source[pos];
                if (s == '"') {
                    advance();
                    closed = true;
                    break;
                }
                if (s == '\\') {
                    advance();
                    if (pos >= source.size()) {
                        break;
                    }
                    const char esc = advance();
                    switch (esc) {
                    case 'n': text.push_back('\n'); break;
                    case 't': text.push_back('\t'); break;
                    case 'r': text.push_back('\r'); break;
                    case '"': text.push_back('"'); break;
                    case '\\': text.push_back('\\'); break;
                    default:
                        throw error(at, "unknown escape sequence '\\" + std::string(1, esc) + "'");
                    }
                    continue;
                }
                text.push_back(advance());
            }
            if (!closed) {
                throw error(at, "unterminated string literal");
            }
            emit(TokenKind::String, std::move(text), at);
            continue;
        }

        // Colour literal.
        if (c == '#') {
            advance();
            std::string text;
            while (isHexDigit(peek(0)) && text.size() < 8) {
                text.push_back(advance());
            }
            if (text.size() != 6 && text.size() != 8) {
                throw error(at, "invalid colour literal (expected #RRGGBB or #RRGGBBAA)");
            }
            emit(TokenKind::Color, "#" + text, at);
            continue;
        }

        // Numbers.
        if (isDigit(c)) {
            std::string text;
            bool isFloat = false;
            if (c == '0' && (peek() == 'x' || peek() == 'X')) {
                advance();
                advance();
                while (isHexDigit(peek(0))) {
                    text.push_back(advance());
                }
                if (text.empty()) {
                    throw error(at, "invalid hexadecimal literal");
                }
                emit(TokenKind::Int, "0x" + text, at);
                continue;
            }
            while (isDigit(peek(0))) {
                text.push_back(advance());
            }
            if (peek(0) == '.' && isDigit(peek(1))) {
                isFloat = true;
                text.push_back(advance()); // .
                while (isDigit(peek(0))) {
                    text.push_back(advance());
                }
            }
            if (peek(0) == 'e' || peek(0) == 'E') {
                isFloat = true;
                text.push_back(advance());
                if (peek(0) == '+' || peek(0) == '-') {
                    text.push_back(advance());
                }
                if (!isDigit(peek(0))) {
                    throw error(at, "invalid exponent in numeric literal");
                }
                while (isDigit(peek(0))) {
                    text.push_back(advance());
                }
            }
            emit(isFloat ? TokenKind::Float : TokenKind::Int, std::move(text), at);
            continue;
        }

        // Identifiers and keywords.
        if (isIdentStart(c)) {
            std::string text;
            while (isIdentPart(peek(0))) {
                text.push_back(advance());
            }
            const auto& table = keywords();
            const auto found = table.find(text);
            emit(found == table.end() ? TokenKind::Identifier : found->second, std::move(text), at);
            continue;
        }

        // Operators and punctuation.
        const auto two = [&](char a, char b, TokenKind kind) {
            if (c == a && peek() == b) {
                advance();
                advance();
                emit(kind, std::string{a, b}, at);
                return true;
            }
            return false;
        };
        const auto one = [&](TokenKind kind) {
            advance();
            emit(kind, std::string(1, c), at);
        };

        if (two('<', '<', TokenKind::Shl)) continue;
        if (two('>', '>', TokenKind::Shr)) continue;
        if (two('=', '=', TokenKind::Equal)) continue;
        if (two('!', '=', TokenKind::NotEqual)) continue;
        if (two('<', '=', TokenKind::LessEqual)) continue;
        if (two('>', '=', TokenKind::GreaterEqual)) continue;
        if (two('&', '&', TokenKind::AndAnd)) continue;
        if (two('|', '|', TokenKind::OrOr)) continue;
        if (two('-', '>', TokenKind::Arrow)) continue;

        switch (c) {
        case '{': one(TokenKind::LBrace); continue;
        case '}': one(TokenKind::RBrace); continue;
        case '(': one(TokenKind::LParen); continue;
        case ')': one(TokenKind::RParen); continue;
        case '[': one(TokenKind::LBracket); continue;
        case ']': one(TokenKind::RBracket); continue;
        case ',': one(TokenKind::Comma); continue;
        case '.': one(TokenKind::Dot); continue;
        case ':': one(TokenKind::Colon); continue;
        case ';': one(TokenKind::Semi); continue;
        case '=': one(TokenKind::Assign); continue;
        case '<': one(TokenKind::Less); continue;
        case '>': one(TokenKind::Greater); continue;
        case '+': one(TokenKind::Plus); continue;
        case '-': one(TokenKind::Minus); continue;
        case '*': one(TokenKind::Star); continue;
        case '/': one(TokenKind::Slash); continue;
        case '%': one(TokenKind::Percent); continue;
        case '^': one(TokenKind::Caret); continue;
        case '&': one(TokenKind::Amp); continue;
        case '|': one(TokenKind::Pipe); continue;
        case '~': one(TokenKind::Tilde); continue;
        case '!': one(TokenKind::Bang); continue;
        default: break;
        }

        throw error(at, std::string("unexpected character '") + c + "'");
    }

    tokens.push_back({TokenKind::End, "", location()});
    return tokens;
}

const char* tokenName(TokenKind kind) {
    switch (kind) {
    case TokenKind::End: return "end of input";
    case TokenKind::Identifier: return "identifier";
    case TokenKind::KeywordAsset: return "asset";
    case TokenKind::KeywordInput: return "input";
    case TokenKind::KeywordOutput: return "output";
    case TokenKind::KeywordImport: return "import";
    case TokenKind::KeywordFrom: return "from";
    case TokenKind::KeywordDraw: return "draw";
    case TokenKind::KeywordAt: return "at";
    case TokenKind::KeywordIf: return "if";
    case TokenKind::KeywordElse: return "else";
    case TokenKind::KeywordWhile: return "while";
    case TokenKind::KeywordReturn: return "return";
    case TokenKind::KeywordTrue: return "true";
    case TokenKind::KeywordFalse: return "false";
    case TokenKind::KeywordList: return "list";
    case TokenKind::TypeColor: return "color";
    case TokenKind::TypeF32: return "f32";
    case TokenKind::TypeU32: return "u32";
    case TokenKind::TypeI32: return "i32";
    case TokenKind::KindPoint: return "point";
    case TokenKind::KindRect: return "rect";
    case TokenKind::KindCircle: return "circle";
    case TokenKind::Int: return "integer literal";
    case TokenKind::Float: return "float literal";
    case TokenKind::Color: return "colour literal";
    case TokenKind::String: return "string literal";
    case TokenKind::LBrace: return "{";
    case TokenKind::RBrace: return "}";
    case TokenKind::LParen: return "(";
    case TokenKind::RParen: return ")";
    case TokenKind::LBracket: return "[";
    case TokenKind::RBracket: return "]";
    case TokenKind::Comma: return ",";
    case TokenKind::Dot: return ".";
    case TokenKind::Colon: return ":";
    case TokenKind::Semi: return ";";
    case TokenKind::Arrow: return "->";
    case TokenKind::Assign: return "=";
    case TokenKind::Equal: return "==";
    case TokenKind::NotEqual: return "!=";
    case TokenKind::Less: return "<";
    case TokenKind::LessEqual: return "<=";
    case TokenKind::Greater: return ">";
    case TokenKind::GreaterEqual: return ">=";
    case TokenKind::Plus: return "+";
    case TokenKind::Minus: return "-";
    case TokenKind::Star: return "*";
    case TokenKind::Slash: return "/";
    case TokenKind::Percent: return "%";
    case TokenKind::Caret: return "^";
    case TokenKind::Amp: return "&";
    case TokenKind::Pipe: return "|";
    case TokenKind::Shl: return "<<";
    case TokenKind::Shr: return ">>";
    case TokenKind::Tilde: return "~";
    case TokenKind::Bang: return "!";
    case TokenKind::AndAnd: return "&&";
    case TokenKind::OrOr: return "||";
    }
    return "?";
}

} // namespace iprop::ips
