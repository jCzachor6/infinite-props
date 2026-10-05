#include "ips/Parser.hpp"

#include <string>
#include <utility>

#include "ips/ParserDetail.hpp"

namespace iprop::ips {
namespace detail {
namespace {

std::unique_ptr<Expr> literal(Literal::Kind literalKind, const Token& token,
                             const std::string& text) {
    return std::make_unique<Expr>(
        Literal{literalKind, text, token.location});
}

std::unique_ptr<Expr> binary(Binary::Op op, std::unique_ptr<Expr> left,
                             std::unique_ptr<Expr> right,
                             const SourceLocation& at) {
    return std::make_unique<Expr>(
        Binary{op, std::move(left), std::move(right), at});
}

} // namespace

// ---- expressions -----------------------------------------------------------
//
// Precedence, loosest to tightest:
//   ||  <  &&  <  |  <  ^  <  &  <  ==/!=  <  </<=/>/>=  <  <</>>
//   <  +,-  <  *,/,%
//   <  unary  <  postfix (member access and calls)

std::unique_ptr<Expr> ParserImpl::parseExpr() { return parseOr(); }

std::unique_ptr<Expr> ParserImpl::parseOr() {
    auto left = parseAnd();
    while (match(TokenKind::OrOr)) {
        left = binary(Binary::Op::Or, std::move(left), parseAnd(),
                     tokens_[pos_ - 1].location);
    }
    return left;
}

std::unique_ptr<Expr> ParserImpl::parseAnd() {
    auto left = parseBitOr();
    while (match(TokenKind::AndAnd)) {
        left = binary(Binary::Op::And, std::move(left), parseBitOr(),
                     tokens_[pos_ - 1].location);
    }
    return left;
}

std::unique_ptr<Expr> ParserImpl::parseBitOr() {
    auto left = parseBitXor();
    while (match(TokenKind::Pipe)) {
        left = binary(Binary::Op::BitOr, std::move(left), parseBitXor(),
                     tokens_[pos_ - 1].location);
    }
    return left;
}

std::unique_ptr<Expr> ParserImpl::parseBitXor() {
    auto left = parseBitAnd();
    while (match(TokenKind::Caret)) {
        left = binary(Binary::Op::Xor, std::move(left), parseBitAnd(),
                     tokens_[pos_ - 1].location);
    }
    return left;
}

std::unique_ptr<Expr> ParserImpl::parseBitAnd() {
    auto left = parseEquality();
    while (match(TokenKind::Amp)) {
        left = binary(Binary::Op::BitAnd, std::move(left), parseEquality(),
                     tokens_[pos_ - 1].location);
    }
    return left;
}

std::unique_ptr<Expr> ParserImpl::parseEquality() {
    auto left = parseComparison();
    for (;;) {
        if (match(TokenKind::Equal)) {
            left = binary(Binary::Op::Eq, std::move(left), parseComparison(), tokens_[pos_ - 1].location);
        } else if (match(TokenKind::NotEqual)) {
            left = binary(Binary::Op::Ne, std::move(left), parseComparison(), tokens_[pos_ - 1].location);
        } else {
            return left;
        }
    }
}

std::unique_ptr<Expr> ParserImpl::parseComparison() {
    auto left = parseShift();
    for (;;) {
        if (match(TokenKind::Less)) {
            left = binary(Binary::Op::Lt, std::move(left), parseShift(), tokens_[pos_ - 1].location);
        } else if (match(TokenKind::LessEqual)) {
            left = binary(Binary::Op::Le, std::move(left), parseShift(), tokens_[pos_ - 1].location);
        } else if (match(TokenKind::Greater)) {
            left = binary(Binary::Op::Gt, std::move(left), parseShift(), tokens_[pos_ - 1].location);
        } else if (match(TokenKind::GreaterEqual)) {
            left = binary(Binary::Op::Ge, std::move(left), parseShift(), tokens_[pos_ - 1].location);
        } else {
            return left;
        }
    }
}

std::unique_ptr<Expr> ParserImpl::parseShift() {
    auto left = parseAdditive();
    for (;;) {
        if (match(TokenKind::Shl)) {
            left = binary(Binary::Op::Shl, std::move(left), parseAdditive(),
                          tokens_[pos_ - 1].location);
        } else if (match(TokenKind::Shr)) {
            left = binary(Binary::Op::Shr, std::move(left), parseAdditive(),
                          tokens_[pos_ - 1].location);
        } else {
            return left;
        }
    }
}

std::unique_ptr<Expr> ParserImpl::parseAdditive() {
    auto left = parseMultiplicative();
    for (;;) {
        if (match(TokenKind::Plus)) {
            left = binary(Binary::Op::Add, std::move(left), parseMultiplicative(), tokens_[pos_ - 1].location);
        } else if (match(TokenKind::Minus)) {
            left = binary(Binary::Op::Sub, std::move(left), parseMultiplicative(), tokens_[pos_ - 1].location);
        } else {
            return left;
        }
    }
}

std::unique_ptr<Expr> ParserImpl::parseMultiplicative() {
    auto left = parseUnary();
    for (;;) {
        if (match(TokenKind::Star)) {
            left = binary(Binary::Op::Mul, std::move(left), parseUnary(), tokens_[pos_ - 1].location);
        } else if (match(TokenKind::Slash)) {
            left = binary(Binary::Op::Div, std::move(left), parseUnary(), tokens_[pos_ - 1].location);
        } else if (match(TokenKind::Percent)) {
            left = binary(Binary::Op::Mod, std::move(left), parseUnary(), tokens_[pos_ - 1].location);
        } else {
            return left;
        }
    }
}

std::unique_ptr<Expr> ParserImpl::parseUnary() {
    if (match(TokenKind::Minus)) {
        const SourceLocation at = tokens_[pos_ - 1].location;
        return std::make_unique<Expr>(Unary{Unary::Op::Neg, parseUnary(), at});
    }
    if (match(TokenKind::Bang)) {
        const SourceLocation at = tokens_[pos_ - 1].location;
        return std::make_unique<Expr>(Unary{Unary::Op::Not, parseUnary(), at});
    }
    if (match(TokenKind::Tilde)) {
        const SourceLocation at = tokens_[pos_ - 1].location;
        return std::make_unique<Expr>(Unary{Unary::Op::BitNot, parseUnary(), at});
    }
    return parsePostfix();
}

std::unique_ptr<Expr> ParserImpl::parsePostfix() {
    return parsePostfixAfter(parsePrimary());
}

// Continues a postfix chain on an already-parsed primary: member access
// ('.' name), calls ('(' args ')') and palette indexing ('[' expr ']').
std::unique_ptr<Expr> ParserImpl::parsePostfixAfter(std::unique_ptr<Expr> expr) {
    for (;;) {
        if (match(TokenKind::Dot)) {
            const Token name = expectName("a member name after '.'");
            expr = std::make_unique<Expr>(
                Member{std::move(expr), name.text, name.location});
        } else if (kind() == TokenKind::LParen) {
            const SourceLocation at = locationOf(*expr);
            expr = std::make_unique<Expr>(Call{std::move(expr), parseArgs(), at});
        } else if (kind() == TokenKind::LBracket) {
            const SourceLocation at = locationOf(*expr);
            advance(); // '['
            auto index = parseExpr();
            expect(TokenKind::RBracket, "']' after the index");
            expr = std::make_unique<Expr>(
                Index{std::move(expr), std::move(index), at});
        } else {
            return expr;
        }
    }
}

std::unique_ptr<Expr> ParserImpl::parsePrimary() {
    if (auto value = parseLiteral()) {
        return value;
    }
    switch (kind()) {
    case TokenKind::Identifier: {
        const Token name = advance();
        return std::make_unique<Expr>(Identifier{name.text, name.location});
    }
    case TokenKind::LBrace:
        return parseVector();
    case TokenKind::LParen: {
        advance();
        auto expr = parseExpr();
        expect(TokenKind::RParen, "')'");
        return expr;
    }
    default:
        // Soft keywords (color, at, point, ...) are valid names, so they
        // also appear as expression identifiers.
        if (isNameKind(kind())) {
            const Token name = advance();
            return std::make_unique<Expr>(Identifier{name.text, name.location});
        }
        failExpected("an expression");
    }
}

// A literal expression if the current token starts one, else null. Shared
// by parsePrimary (full expressions) and parseLiteralValue (input defaults,
// which accept a literal optionally wrapped by parentheses/one unary minus).
std::unique_ptr<Expr> ParserImpl::parseLiteral() {
    switch (kind()) {
    case TokenKind::Int: {
        const Token token = advance();
        return literal(Literal::Kind::Int, token, token.text);
    }
    case TokenKind::Float: {
        const Token token = advance();
        return literal(Literal::Kind::Float, token, token.text);
    }
    case TokenKind::Color: {
        const Token token = advance();
        return literal(Literal::Kind::Color, token, token.text);
    }
    case TokenKind::String: {
        const Token token = advance();
        return literal(Literal::Kind::String, token, token.text);
    }
    case TokenKind::KeywordTrue: {
        const Token token = advance();
        return literal(Literal::Kind::True, token, "true");
    }
    case TokenKind::KeywordFalse: {
        const Token token = advance();
        return literal(Literal::Kind::False, token, "false");
    }
    default:
        return nullptr;
    }
}

std::unique_ptr<Expr> ParserImpl::parseVector() {
    const SourceLocation at = expect(TokenKind::LBrace, "'{'").location;
    std::vector<std::unique_ptr<Expr>> elements;
    while (kind() != TokenKind::RBrace) {
        if (kind() == TokenKind::End) {
            failHere("unterminated vector literal (missing '}')");
        }
        elements.push_back(parseExpr());
        if (!match(TokenKind::Comma)) {
            break;
        }
        // Allow a trailing comma.
        if (kind() == TokenKind::RBrace) {
            break;
        }
    }
    expect(TokenKind::RBrace, "'}'");
    return std::make_unique<Expr>(VectorLit{std::move(elements), at});
}

std::vector<Arg> ParserImpl::parseArgs() {
    expect(TokenKind::LParen, "'('");
    std::vector<Arg> args;
    while (kind() != TokenKind::RParen) {
        if (isNameKind(kind()) && peek(1).kind == TokenKind::Assign) {
            const Token name = advance();
            advance(); // '='
            args.push_back(Arg{name.text, parseExpr(), name.location});
        } else {
            const SourceLocation at = peek().location;
            args.push_back(Arg{std::nullopt, parseExpr(), at});
        }
        if (!match(TokenKind::Comma)) {
            break;
        }
    }
    expect(TokenKind::RParen, "')'");
    return args;
}

} // namespace detail
} // namespace iprop::ips
