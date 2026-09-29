#include "ips/Parser.hpp"

#include <string>
#include <utility>

#include "ips/ParserDetail.hpp"

namespace iprop::ips {
namespace detail {
namespace {

std::unique_ptr<Expr> literal(Literal::Kind literalKind, std::string text) {
    return std::make_unique<Expr>(Literal{literalKind, std::move(text)});
}

std::unique_ptr<Expr> binary(Binary::Op op, std::unique_ptr<Expr> left,
                             std::unique_ptr<Expr> right) {
    return std::make_unique<Expr>(Binary{op, std::move(left), std::move(right)});
}

} // namespace

// ---- expressions -----------------------------------------------------------
//
// Precedence, loosest to tightest:
//   ||  <  &&  <  |  <  ^  <  &  <  ==/!=  <  </<=/>/>=  <  +,-  <  *,/,%
//   <  unary  <  postfix (member access and calls)

std::unique_ptr<Expr> ParserImpl::parseExpr() { return parseOr(); }

std::unique_ptr<Expr> ParserImpl::parseOr() {
    auto left = parseAnd();
    while (match(TokenKind::OrOr)) {
        left = binary(Binary::Op::Or, std::move(left), parseAnd());
    }
    return left;
}

std::unique_ptr<Expr> ParserImpl::parseAnd() {
    auto left = parseBitOr();
    while (match(TokenKind::AndAnd)) {
        left = binary(Binary::Op::And, std::move(left), parseBitOr());
    }
    return left;
}

std::unique_ptr<Expr> ParserImpl::parseBitOr() {
    auto left = parseBitXor();
    while (match(TokenKind::Pipe)) {
        left = binary(Binary::Op::BitOr, std::move(left), parseBitXor());
    }
    return left;
}

std::unique_ptr<Expr> ParserImpl::parseBitXor() {
    auto left = parseBitAnd();
    while (match(TokenKind::Caret)) {
        left = binary(Binary::Op::Xor, std::move(left), parseBitAnd());
    }
    return left;
}

std::unique_ptr<Expr> ParserImpl::parseBitAnd() {
    auto left = parseEquality();
    while (match(TokenKind::Amp)) {
        left = binary(Binary::Op::BitAnd, std::move(left), parseEquality());
    }
    return left;
}

std::unique_ptr<Expr> ParserImpl::parseEquality() {
    auto left = parseComparison();
    for (;;) {
        if (match(TokenKind::Equal)) {
            left = binary(Binary::Op::Eq, std::move(left), parseComparison());
        } else if (match(TokenKind::NotEqual)) {
            left = binary(Binary::Op::Ne, std::move(left), parseComparison());
        } else {
            return left;
        }
    }
}

std::unique_ptr<Expr> ParserImpl::parseComparison() {
    auto left = parseAdditive();
    for (;;) {
        if (match(TokenKind::Less)) {
            left = binary(Binary::Op::Lt, std::move(left), parseAdditive());
        } else if (match(TokenKind::LessEqual)) {
            left = binary(Binary::Op::Le, std::move(left), parseAdditive());
        } else if (match(TokenKind::Greater)) {
            left = binary(Binary::Op::Gt, std::move(left), parseAdditive());
        } else if (match(TokenKind::GreaterEqual)) {
            left = binary(Binary::Op::Ge, std::move(left), parseAdditive());
        } else {
            return left;
        }
    }
}

std::unique_ptr<Expr> ParserImpl::parseAdditive() {
    auto left = parseMultiplicative();
    for (;;) {
        if (match(TokenKind::Plus)) {
            left = binary(Binary::Op::Add, std::move(left), parseMultiplicative());
        } else if (match(TokenKind::Minus)) {
            left = binary(Binary::Op::Sub, std::move(left), parseMultiplicative());
        } else {
            return left;
        }
    }
}

std::unique_ptr<Expr> ParserImpl::parseMultiplicative() {
    auto left = parseUnary();
    for (;;) {
        if (match(TokenKind::Star)) {
            left = binary(Binary::Op::Mul, std::move(left), parseUnary());
        } else if (match(TokenKind::Slash)) {
            left = binary(Binary::Op::Div, std::move(left), parseUnary());
        } else if (match(TokenKind::Percent)) {
            left = binary(Binary::Op::Mod, std::move(left), parseUnary());
        } else {
            return left;
        }
    }
}

std::unique_ptr<Expr> ParserImpl::parseUnary() {
    if (match(TokenKind::Minus)) {
        return std::make_unique<Expr>(Unary{Unary::Op::Neg, parseUnary()});
    }
    if (match(TokenKind::Bang)) {
        return std::make_unique<Expr>(Unary{Unary::Op::Not, parseUnary()});
    }
    if (match(TokenKind::Tilde)) {
        return std::make_unique<Expr>(Unary{Unary::Op::BitNot, parseUnary()});
    }
    return parsePostfix();
}

std::unique_ptr<Expr> ParserImpl::parsePostfix() {
    return parsePostfixAfter(parsePrimary());
}

// Continues a postfix chain on an already-parsed primary: member access
// ('.' name) and calls ('(' args ')').
std::unique_ptr<Expr> ParserImpl::parsePostfixAfter(std::unique_ptr<Expr> expr) {
    for (;;) {
        if (match(TokenKind::Dot)) {
            const Token name = expectName("a member name after '.'");
            expr = std::make_unique<Expr>(Member{std::move(expr), name.text});
        } else if (kind() == TokenKind::LParen) {
            expr = std::make_unique<Expr>(Call{std::move(expr), parseArgs()});
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
    case TokenKind::Identifier:
        return std::make_unique<Expr>(Identifier{advance().text});
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
            return std::make_unique<Expr>(Identifier{advance().text});
        }
        failExpected("an expression");
    }
}

// A literal expression if the current token starts one, else null. Shared
// by parsePrimary (full expressions) and parseLiteralValue (input defaults,
// which only allow literals).
std::unique_ptr<Expr> ParserImpl::parseLiteral() {
    switch (kind()) {
    case TokenKind::Int:
        return literal(Literal::Kind::Int, advance().text);
    case TokenKind::Float:
        return literal(Literal::Kind::Float, advance().text);
    case TokenKind::Color:
        return literal(Literal::Kind::Color, advance().text);
    case TokenKind::String:
        return literal(Literal::Kind::String, advance().text);
    case TokenKind::KeywordTrue:
        advance();
        return literal(Literal::Kind::True, "true");
    case TokenKind::KeywordFalse:
        advance();
        return literal(Literal::Kind::False, "false");
    default:
        return nullptr;
    }
}

std::unique_ptr<Expr> ParserImpl::parseVector() {
    expect(TokenKind::LBrace, "'{'");
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
    return std::make_unique<Expr>(VectorLit{std::move(elements)});
}

std::vector<Arg> ParserImpl::parseArgs() {
    expect(TokenKind::LParen, "'('");
    std::vector<Arg> args;
    while (kind() != TokenKind::RParen) {
        if (isNameKind(kind()) && peek(1).kind == TokenKind::Assign) {
            const Token name = advance();
            advance(); // '='
            args.push_back(Arg{name.text, parseExpr()});
        } else {
            args.push_back(Arg{std::nullopt, parseExpr()});
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
