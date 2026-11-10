#include "iprop/math/Distribution.hpp"
#include "ips/Parser.hpp"

#include <optional>
#include <string>
#include <utility>

#include "ips/ParserDetail.hpp"

namespace iprop::ips {
namespace detail {
namespace {

std::unique_ptr<Expr> literal(Literal::Kind literalKind, const Token& token,
                             const std::string& text) {
    return node(
        Literal{literalKind, text, token.location});
}

// `at` is the operator. Callers read it before parsing the right operand:
// argument evaluation order is unspecified, and the location must not
// depend on the host compiler.
std::unique_ptr<Expr> binary(Binary::Op op, std::unique_ptr<Expr> left,
                             std::unique_ptr<Expr> right,
                             const SourceLocation& at) {
    const TokenRange range{left->sourceRange.begin, right->sourceRange.end};
    auto expr = node(
        Binary{op, std::move(left), std::move(right), at});
    expr->sourceRange = expr->sourceCoreRange = range;
    return expr;
}

} // namespace

// ---- expressions -----------------------------------------------------------
//
// Precedence, loosest to tightest:
//   ||  <  &&  <  ==/!=  <  </<=/>/>=  <  to  <  |  <  ^  <  &  <  <</>>
//   <  +,-  <  *,/,%
//   <  unary  <  postfix (member access and calls)
// Bitwise operators bind tighter than comparisons, so `x & 1 == 0` tests the
// masked value. Equality, ordering and `to` do not chain.

std::unique_ptr<Expr> ParserImpl::parseExpr() { return parseOr(); }

std::unique_ptr<Expr> ParserImpl::parseOr() {
    auto left = parseAnd();
    while (match(TokenKind::OrOr)) {
        const auto at = tokens_[pos_ - 1].location;
        left = binary(Binary::Op::Or, std::move(left), parseAnd(), at);
    }
    return left;
}

std::unique_ptr<Expr> ParserImpl::parseAnd() {
    auto left = parseEquality();
    while (match(TokenKind::AndAnd)) {
        const auto at = tokens_[pos_ - 1].location;
        left = binary(Binary::Op::And, std::move(left), parseEquality(), at);
    }
    return left;
}

std::unique_ptr<Expr> ParserImpl::parseBitOr() {
    auto left = parseBitXor();
    while (match(TokenKind::Pipe)) {
        const auto at = tokens_[pos_ - 1].location;
        left = binary(Binary::Op::BitOr, std::move(left), parseBitXor(), at);
    }
    return left;
}

std::unique_ptr<Expr> ParserImpl::parseBitXor() {
    auto left = parseBitAnd();
    while (match(TokenKind::Caret)) {
        const auto at = tokens_[pos_ - 1].location;
        left = binary(Binary::Op::Xor, std::move(left), parseBitAnd(), at);
    }
    return left;
}

std::unique_ptr<Expr> ParserImpl::parseBitAnd() {
    auto left = parseShift();
    while (match(TokenKind::Amp)) {
        const auto at = tokens_[pos_ - 1].location;
        left = binary(Binary::Op::BitAnd, std::move(left), parseShift(), at);
    }
    return left;
}

// `a == b == c` would compare a 0/1 result with c. Like `to`, it is rejected.
std::unique_ptr<Expr> ParserImpl::parseEquality() {
    auto left = parseComparison();
    const auto op = kind() == TokenKind::Equal ? Binary::Op::Eq : Binary::Op::Ne;
    if (!match(TokenKind::Equal) && !match(TokenKind::NotEqual)) return left;
    const auto at = tokens_[pos_ - 1].location;
    left = binary(op, std::move(left), parseComparison(), at);
    if (kind() == TokenKind::Equal || kind() == TokenKind::NotEqual)
        failHere("'==' and '!=' cannot be chained; compare each pair and join them with &&, "
                 "or parenthesize the comparison whose 0/1 result is compared");
    return left;
}

// `a < b < c` would compare the 0/1 result of `a < b` with c.
std::unique_ptr<Expr> ParserImpl::parseComparison() {
    const auto ordering = [&]() -> std::optional<Binary::Op> {
        switch (kind()) {
        case TokenKind::Less: return Binary::Op::Lt;
        case TokenKind::LessEqual: return Binary::Op::Le;
        case TokenKind::Greater: return Binary::Op::Gt;
        case TokenKind::GreaterEqual: return Binary::Op::Ge;
        default: return std::nullopt;
        }
    };
    auto left = parseRandomRange();
    const auto op = ordering();
    if (!op) return left;
    const auto at = advance().location;
    left = binary(*op, std::move(left), parseRandomRange(), at);
    if (ordering())
        failHere("comparisons cannot be chained; write a < b && b < c, "
                 "or parenthesize the comparison whose 0/1 result is compared");
    return left;
}

std::unique_ptr<Expr> ParserImpl::parseRandomRange() {
    auto lower = parseBitOr();
    if (kind() != TokenKind::KeywordTo) return lower;
    const SourceLocation at = advance().location; // 'to'
    auto upper = parseBitOr();
    auto end = upper->sourceRange.end;
    std::unique_ptr<Expr> distribution;
    SourceLocation withAt{};
    if (kind() == TokenKind::KeywordWith) {
        withAt = advance().location; // 'with'
        distribution = parseBitOr();
        end = distribution->sourceRange.end;
    }
    if (kind() == TokenKind::KeywordTo) {
        failHere("'to' cannot be chained; parenthesize the range used as a bound, "
                 "for example (a to b) to c");
    }
    const TokenRange range{lower->sourceRange.begin, end};
    auto expr = node(
        RandomRange{std::move(lower), std::move(upper), at, std::move(distribution), withAt});
    expr->sourceRange = expr->sourceCoreRange = range;
    return expr;
}

std::unique_ptr<Expr> ParserImpl::parseShift() {
    auto left = parseAdditive();
    for (;;) {
        if (match(TokenKind::Shl)) {
            const auto at = tokens_[pos_ - 1].location;
            left = binary(Binary::Op::Shl, std::move(left), parseAdditive(), at);
        } else if (match(TokenKind::Shr)) {
            const auto at = tokens_[pos_ - 1].location;
            left = binary(Binary::Op::Shr, std::move(left), parseAdditive(), at);
        } else {
            return left;
        }
    }
}

std::unique_ptr<Expr> ParserImpl::parseAdditive() {
    auto left = parseMultiplicative();
    for (;;) {
        if (match(TokenKind::Plus)) {
            const auto at = tokens_[pos_ - 1].location;
            left = binary(Binary::Op::Add, std::move(left), parseMultiplicative(), at);
        } else if (match(TokenKind::Minus)) {
            const auto at = tokens_[pos_ - 1].location;
            left = binary(Binary::Op::Sub, std::move(left), parseMultiplicative(), at);
        } else {
            return left;
        }
    }
}

std::unique_ptr<Expr> ParserImpl::parseMultiplicative() {
    auto left = parseUnary();
    for (;;) {
        if (match(TokenKind::Star)) {
            const auto at = tokens_[pos_ - 1].location;
            left = binary(Binary::Op::Mul, std::move(left), parseUnary(), at);
        } else if (match(TokenKind::Slash)) {
            const auto at = tokens_[pos_ - 1].location;
            left = binary(Binary::Op::Div, std::move(left), parseUnary(), at);
        } else if (match(TokenKind::Percent)) {
            const auto at = tokens_[pos_ - 1].location;
            left = binary(Binary::Op::Mod, std::move(left), parseUnary(), at);
        } else {
            return left;
        }
    }
}

std::unique_ptr<Expr> ParserImpl::parseUnary() {
    const Nesting level(*this, expressionNesting_, kExpressionNesting);
    const auto start = pos_;
    if (match(TokenKind::Minus)) {
        const SourceLocation at = tokens_[pos_ - 1].location;
        auto expr = node(Unary{Unary::Op::Neg, parseUnary(), at});
        expr->sourceRange = expr->sourceCoreRange = {start, pos_};
        return expr;
    }
    if (match(TokenKind::Bang)) {
        const SourceLocation at = tokens_[pos_ - 1].location;
        auto expr = node(Unary{Unary::Op::Not, parseUnary(), at});
        expr->sourceRange = expr->sourceCoreRange = {start, pos_};
        return expr;
    }
    if (match(TokenKind::Tilde)) {
        const SourceLocation at = tokens_[pos_ - 1].location;
        auto expr = node(Unary{Unary::Op::BitNot, parseUnary(), at});
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
            const auto* base = std::get_if<Identifier>(expr.get());
            const Token name = base && ((base->name == "mask" && kind() == TokenKind::KeywordFrom) ||
                ((base->name == "noise" || base->name == "regions" || base->name == "part") && kind() == TokenKind::KeywordMask)) ?
                advance() : expectName("a member name after '.'");
            expr = node(
                Member{std::move(expr), name.text, name.location});
        } else if (kind() == TokenKind::LParen) {
            const SourceLocation at = locationOf(*expr);
            auto args = parseArgs();
            auto* member = std::get_if<Member>(expr.get());
            if (member && member->name == "pick") {
                if (args.size() > 1 || (!args.empty() && args[0].name && *args[0].name != "n"))
                    throw SourceError{at, ".pick takes no arguments or one i32 count named 'n'"};
                expr = node(SamplerPick{std::move(member->object),
                    args.empty() ? nullptr : std::move(args[0].value), at});
            } else if (member && member->name == "blur") {
                if (args.size() != 1 || (args[0].name && *args[0].name != "sigma"))
                    throw SourceError{at, ".blur takes one f32 sigma, optionally named 'sigma'"};
                expr = node(SurfaceBlur{std::move(member->object), std::move(args[0]), at});
            } else if (member && (member->name == "distinct" || member->name == "sort")) {
                if (!args.empty()) throw SourceError{at, "." + member->name + " takes no arguments"};
                expr = node(ListMethod{std::move(member->object), member->name, at});
            } else {
                expr = node(Call{std::move(expr), std::move(args), at});
            }
        } else if (kind() == TokenKind::LBracket) {
            const SourceLocation at = locationOf(*expr);
            advance(); // '['
            auto index = parseExpr();
            expect(TokenKind::RBracket, "']' after the index");
            expr = node(
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
    case TokenKind::KeywordWeighted:
    case TokenKind::KeywordDensity:
        return parsePointDistribution();
    case TokenKind::KeywordSampler:
    case TokenKind::KeywordStepper: {
        const bool stepper = kind() == TokenKind::KeywordStepper;
        const auto at = advance().location;
        auto args = parseArgs();
        if (args.size() != 1 || args[0].name)
            throw SourceError{at, std::string(stepper ? "stepper" : "sampler") + " capture takes one positional value or range"};
        auto expr = node(SamplerCapture{std::move(args[0].value), at, stepper});
        expr->sourceRange = expr->sourceCoreRange = {start, pos_};
        return expr;
    }
    case TokenKind::KeywordMask:
    case TokenKind::Identifier: {
        const Token name = advance();
        auto expr = node(Identifier{name.text, name.location});
        expr->sourceRange = expr->sourceCoreRange = {start, pos_};
        return expr;
    }
    case TokenKind::LBrace:
        return parseVector();
    case TokenKind::LBracket:
        return parseList();
    case TokenKind::LParen: {
        advance();
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
            auto expr = node(Identifier{name.text, name.location});
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
    // `3px` lexes as a number and a word; no expression continues that way.
    const auto rejectSuffix = [&](const Token& number) {
        const Token& next = peek();
        const char first = next.text.empty() ? '\0' : next.text[0];
        const bool word = (first >= 'a' && first <= 'z') || (first >= 'A' && first <= 'Z') || first == '_';
        if (word && next.location.byteOffset == number.location.endByteOffset)
            throw SourceError{next.location, "unexpected '" + next.text + "' directly after a number"};
    };
    switch (kind()) {
    case TokenKind::Int: {
        const Token token = advance();
        rejectSuffix(token);
        return rangedLiteral(Literal::Kind::Int, token, token.text);
    }
    case TokenKind::Float: {
        const Token token = advance();
        rejectSuffix(token);
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
    auto expr = node(VectorLit{std::move(elements), at});
    expr->sourceRange = expr->sourceCoreRange = {start, pos_};
    return expr;
}

std::unique_ptr<Expr> ParserImpl::parseList() {
    const auto start = pos_;
    const auto at = expect(TokenKind::LBracket, "'['").location;
    std::vector<std::unique_ptr<Expr>> elements;
    while (kind() != TokenKind::RBracket) {
        if (kind() == TokenKind::End) failHere("unterminated list literal (missing ']')");
        elements.push_back(parseExpr());
        if (!match(TokenKind::Comma)) break;
    }
    expect(TokenKind::RBracket, "']' after list elements");
    auto expr = node(ListLit{std::move(elements), at});
    expr->sourceRange = expr->sourceCoreRange = {start, pos_};
    return expr;
}

std::unique_ptr<Expr> ParserImpl::parsePointDistribution() {
    const auto start = pos_;
    const auto name = advance();
    expect(TokenKind::LBrace, "'{' after distribution family");
    PointDistributionLiteral literal; literal.location = name.location;
    literal.family = math::pointDistributionSpec(name.text)->family;
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
    auto expr = node(std::move(literal));
    expr->sourceRange = expr->sourceCoreRange = {start, pos_};
    return expr;
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
