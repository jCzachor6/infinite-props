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
    const TokenRange range{left->sourceRange.begin, right->sourceRange.end};
    auto expr = std::make_unique<Expr>(
        Binary{op, std::move(left), std::move(right), at});
    expr->sourceRange = expr->sourceCoreRange = range;
    return expr;
}

} // namespace

// ---- expressions -----------------------------------------------------------
//
// Precedence, loosest to tightest:
//   ||  <  &&  <  |  <  ^  <  &  <  ==/!=  <  </<=/>/>=  <  to  <  <</>>
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
    auto left = parseRandomRange();
    for (;;) {
        if (match(TokenKind::Less)) {
            left = binary(Binary::Op::Lt, std::move(left), parseRandomRange(), tokens_[pos_ - 1].location);
        } else if (match(TokenKind::LessEqual)) {
            left = binary(Binary::Op::Le, std::move(left), parseRandomRange(), tokens_[pos_ - 1].location);
        } else if (match(TokenKind::Greater)) {
            left = binary(Binary::Op::Gt, std::move(left), parseRandomRange(), tokens_[pos_ - 1].location);
        } else if (match(TokenKind::GreaterEqual)) {
            left = binary(Binary::Op::Ge, std::move(left), parseRandomRange(), tokens_[pos_ - 1].location);
        } else {
            return left;
        }
    }
}

// `to` and `with` are contextual, so old programs keep their meaning. Where
// the next statement or declaration may begin, `to = ...`, `to.draw(...)`
// and a call of a declared helper `to` start it (likewise for `with`);
// everywhere else the word is the operator.
bool ParserImpl::atContextualOperator(std::string_view word, bool helper) const {
    if (kind() != TokenKind::Identifier || peek().text != word) return false;
    if (!statementTail_) return true;
    switch (kind(1)) {
    case TokenKind::Assign:
    case TokenKind::Dot: return false;
    case TokenKind::LParen: return !helper;
    default: return true;
    }
}

std::unique_ptr<Expr> ParserImpl::parseRandomRange() {
    auto lower = parseShift();
    if (!atContextualOperator("to", toHelper_)) return lower;
    const SourceLocation at = advance().location; // 'to'
    auto upper = parseShift();
    auto end = upper->sourceRange.end;
    std::unique_ptr<Expr> distribution;
    SourceLocation withAt{};
    if (atContextualOperator("with", withHelper_)) {
        withAt = advance().location; // 'with'
        distribution = parseShift();
        end = distribution->sourceRange.end;
    }
    if (atContextualOperator("to", toHelper_)) {
        failHere("'to' cannot be chained; parenthesize the range used as a bound, "
                 "for example (a to b) to c");
    }
    const TokenRange range{lower->sourceRange.begin, end};
    auto expr = std::make_unique<Expr>(
        RandomRange{std::move(lower), std::move(upper), at, std::move(distribution), withAt});
    expr->sourceRange = expr->sourceCoreRange = range;
    return expr;
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
    const auto start = pos_;
    if (match(TokenKind::Minus)) {
        const SourceLocation at = tokens_[pos_ - 1].location;
        auto expr = std::make_unique<Expr>(Unary{Unary::Op::Neg, parseUnary(), at});
        expr->sourceRange = expr->sourceCoreRange = {start, pos_};
        return expr;
    }
    if (match(TokenKind::Bang)) {
        const SourceLocation at = tokens_[pos_ - 1].location;
        auto expr = std::make_unique<Expr>(Unary{Unary::Op::Not, parseUnary(), at});
        expr->sourceRange = expr->sourceCoreRange = {start, pos_};
        return expr;
    }
    if (match(TokenKind::Tilde)) {
        const SourceLocation at = tokens_[pos_ - 1].location;
        auto expr = std::make_unique<Expr>(Unary{Unary::Op::BitNot, parseUnary(), at});
        expr->sourceRange = expr->sourceCoreRange = {start, pos_};
        return expr;
    }
    return parsePostfix();
}

std::unique_ptr<Expr> ParserImpl::parsePostfix() {
    return parsePostfixAfter(parsePrimary());
}

// Continues a postfix chain on an already-parsed primary: member access
// ('.' name), calls ('(' args ')') and palette indexing ('[' expr ']').
std::unique_ptr<Expr> ParserImpl::parsePostfixAfter(std::unique_ptr<Expr> expr) {
    const auto start = expr->sourceRange.begin;
    for (;;) {
        if (match(TokenKind::Dot)) {
            const Token name = expectName("a member name after '.'");
            expr = std::make_unique<Expr>(
                Member{std::move(expr), name.text, name.location});
        } else if (kind() == TokenKind::LParen) {
            const SourceLocation at = locationOf(*expr);
            auto args = parseArgs();
            const auto* name = std::get_if<Identifier>(expr.get());
            auto* member = std::get_if<Member>(expr.get());
            if (name && name->name == "sampler" && !samplerHelper_) {
                if (args.size() != 1 || args[0].name)
                    throw SourceError{at, "sampler capture takes one positional value or range"};
                expr = std::make_unique<Expr>(SamplerCapture{std::move(args[0].value), at});
            } else if (member && member->name == "pick") {
                if (args.size() > 1 || (!args.empty() && args[0].name && *args[0].name != "n"))
                    throw SourceError{at, ".pick takes no arguments or one i32 count named 'n'"};
                expr = std::make_unique<Expr>(SamplerPick{std::move(member->object),
                    args.empty() ? nullptr : std::move(args[0].value), at});
            } else {
                if (name && name->name == "sampler" && samplerHelper_)
                    for (const auto& arg : args) if (std::holds_alternative<RandomRange>(*arg.value))
                        throw SourceError{at, "sampler capture conflicts with helper 'sampler'; rename the helper before capturing a range"};
                expr = std::make_unique<Expr>(Call{std::move(expr), std::move(args), at});
            }
        } else if (kind() == TokenKind::LBracket) {
            const SourceLocation at = locationOf(*expr);
            advance(); // '['
            const FlagScope nested(statementTail_, false);
            auto index = parseExpr();
            expect(TokenKind::RBracket, "']' after the index");
            expr = std::make_unique<Expr>(
                Index{std::move(expr), std::move(index), at});
        } else {
            return expr;
        }
        expr->sourceRange = expr->sourceCoreRange = {start, pos_};
    }
}

std::unique_ptr<Expr> ParserImpl::parsePrimary() {
    const auto start = pos_;
    if (auto value = parseLiteral()) {
        return value;
    }
    switch (kind()) {
    case TokenKind::Identifier: {
        if (peek().text == "weighted" && kind(1) == TokenKind::LBrace) return parsePointDistribution();
        const Token name = advance();
        auto expr = std::make_unique<Expr>(Identifier{name.text, name.location});
        expr->sourceRange = expr->sourceCoreRange = {start, pos_};
        return expr;
    }
    case TokenKind::LBrace:
        return parseVector();
    case TokenKind::LBracket:
        return parseList();
    case TokenKind::LParen: {
        advance();
        const FlagScope nested(statementTail_, false);
        auto expr = parseExpr();
        expect(TokenKind::RParen, "')'");
        expr->sourceRange = {start, pos_};
        return expr;
    }
    default:
        // Soft keywords (color, at, point, ...) are valid names, so they
        // also appear as expression identifiers.
        if (isNameKind(kind())) {
            const Token name = advance();
            auto expr = std::make_unique<Expr>(Identifier{name.text, name.location});
            expr->sourceRange = expr->sourceCoreRange = {start, pos_};
            return expr;
        }
        failExpected("an expression");
    }
}

// A literal expression if the current token starts one, else null. Shared
// by parsePrimary (full expressions) and parseLiteralValue (input defaults,
// which accept a literal optionally wrapped by parentheses/one unary minus).
std::unique_ptr<Expr> ParserImpl::parseLiteral() {
    const auto start = pos_;
    const auto rangedLiteral = [&](Literal::Kind kind, const Token& token, const std::string& text) {
        auto expr = literal(kind, token, text);
        expr->sourceRange = expr->sourceCoreRange = {start, pos_};
        return expr;
    };
    switch (kind()) {
    case TokenKind::Int: {
        const Token token = advance();
        return rangedLiteral(Literal::Kind::Int, token, token.text);
    }
    case TokenKind::Float: {
        const Token token = advance();
        return rangedLiteral(Literal::Kind::Float, token, token.text);
    }
    case TokenKind::Color: {
        const Token token = advance();
        return rangedLiteral(Literal::Kind::Color, token, token.text);
    }
    case TokenKind::String: {
        const Token token = advance();
        return rangedLiteral(Literal::Kind::String, token, token.text);
    }
    case TokenKind::KeywordTrue: {
        const Token token = advance();
        return rangedLiteral(Literal::Kind::True, token, "true");
    }
    case TokenKind::KeywordFalse: {
        const Token token = advance();
        return rangedLiteral(Literal::Kind::False, token, "false");
    }
    default:
        return nullptr;
    }
}

std::unique_ptr<Expr> ParserImpl::parseVector() {
    const auto start = pos_;
    const FlagScope nested(statementTail_, false);
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
    auto expr = std::make_unique<Expr>(VectorLit{std::move(elements), at});
    expr->sourceRange = expr->sourceCoreRange = {start, pos_};
    return expr;
}

std::unique_ptr<Expr> ParserImpl::parseList() {
    const auto start = pos_;
    const FlagScope nested(statementTail_, false);
    const auto at = expect(TokenKind::LBracket, "'['").location;
    std::vector<std::unique_ptr<Expr>> elements;
    while (kind() != TokenKind::RBracket) {
        if (kind() == TokenKind::End) failHere("unterminated list literal (missing ']')");
        elements.push_back(parseExpr());
        if (!match(TokenKind::Comma)) break;
    }
    expect(TokenKind::RBracket, "']' after list elements");
    auto expr = std::make_unique<Expr>(ListLit{std::move(elements), at});
    expr->sourceRange = expr->sourceCoreRange = {start, pos_};
    return expr;
}

std::unique_ptr<Expr> ParserImpl::parsePointDistribution() {
    const auto start = pos_;
    const auto name = advance();
    const FlagScope nested(statementTail_, false);
    expect(TokenKind::LBrace, "'{' after distribution family");
    PointDistributionLiteral literal; literal.location = name.location;
    while (kind() != TokenKind::RBrace) {
        if (kind() == TokenKind::End) failHere("unterminated point distribution (missing '}')");
        const auto at = peek().location;
        auto key = parseExpr();
        expect(TokenKind::Colon, "':' between distribution key and value");
        auto value = parseExpr();
        literal.points.push_back({std::move(key), std::move(value), at});
        if (!match(TokenKind::Comma)) break;
    }
    expect(TokenKind::RBrace, "'}' after distribution points");
    auto expr = std::make_unique<Expr>(std::move(literal));
    expr->sourceRange = expr->sourceCoreRange = {start, pos_};
    return expr;
}

std::vector<Arg> ParserImpl::parseArgs() {
    expect(TokenKind::LParen, "'('");
    const FlagScope nested(statementTail_, false);
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
