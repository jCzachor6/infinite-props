#include "ips/Parser.hpp"

#include <string>
#include <utility>

#include "ips/ParserDetail.hpp"

namespace iprop::ips {
namespace detail {

Program ParserImpl::parseProgram() {
    Program program;
    while (kind() == TokenKind::KeywordImport) {
        program.imports.push_back(parseImport());
    }
    if (kind() != TokenKind::KeywordAsset) {
        failExpected("an 'asset' block");
    }
    program.asset = parseAsset();
    if (kind() != TokenKind::End) {
        if (kind() == TokenKind::KeywordAsset) {
            failHere("multiple asset blocks in one file");
        }
        failExpected("end of file");
    }
    return program;
}

// ---- declarations -----------------------------------------------------------

Import ParserImpl::parseImport() {
    const SourceLocation at = advance().location; // 'import'
    const Token alias = expectName("an import alias");
    expect(TokenKind::KeywordFrom, "'from'");
    const Token path = expect(TokenKind::String, "a quoted file path");
    match(TokenKind::Semi);
    return Import{alias.text, path.text, at};
}

Asset ParserImpl::parseAsset() {
    const SourceLocation at = advance().location; // 'asset'
    const Token name = expect(TokenKind::Identifier, "an asset name");
    expect(TokenKind::LBrace, "'{'");
    Asset asset;
    asset.name = name.text;
    asset.location = at;

    while (kind() != TokenKind::RBrace) {
        if (kind() == TokenKind::End) {
            failHere("unterminated asset block (missing '}')");
        }
        switch (kind()) {
        case TokenKind::KeywordInput:
            asset.inputs.push_back(parseInput());
            break;
        case TokenKind::KeywordOutput:
            asset.outputs.push_back(parseOutput());
            break;
        case TokenKind::KeywordDraw:
            asset.functions.push_back(parseFunction());
            break;
        case TokenKind::Identifier: {
            const Token declaration = advance();
            if (declaration.text == "size") {
                parseSize(asset);
            } else if (declaration.text == "origin") {
                parseOrigin(asset);
            } else if (declaration.text == "variants") {
                asset.variants = toU32(expect(TokenKind::Int, "a variant count"),
                                       "variant count");
                match(TokenKind::Semi);
            } else if (declaration.text == "archetypes") {
                asset.archetypes =
                    toU32(expect(TokenKind::Int, "an archetype count"),
                          "archetype count");
                match(TokenKind::Semi);
            } else if (declaration.text == "palette") {
                asset.palettes.push_back(parsePalette());
            } else {
                const auto hint = keywordSuggestion(declaration.text);
                if (kind() != TokenKind::LParen && !hint.empty())
                    throw SourceError{declaration.location, "unknown asset declaration '" + declaration.text + "'" + hint};
                asset.functions.push_back(parseFunctionAfterName(declaration));
            }
            break;
        }
        default:
            // Soft keywords may also name a function.
            if (isNameKind(kind())) {
                asset.functions.push_back(parseFunctionAfterName(advance()));
            } else {
                failExpected("a declaration inside the asset block");
            }
        }
    }
    advance(); // '}'
    return asset;
}

void ParserImpl::parseSize(Asset& asset) {
    const Token width = expect(TokenKind::Int, "a width");
    asset.sizeX = toU32(width, "width");

    // "192x192" lexes as Int "192" then Identifier "x192".
    if (kind() == TokenKind::Int) {
        asset.sizeY = toU32(advance(), "height");
    } else if (kind() == TokenKind::Identifier) {
        const Token height = advance();
        if (height.text.size() > 1 &&
            (height.text[0] == 'x' || height.text[0] == 'X') &&
            isDigits(std::string_view(height.text).substr(1))) {
            asset.sizeY = toU32(
                Token{TokenKind::Int, height.text.substr(1), height.location},
                "height");
        } else {
            failHere("expected a height after the width, e.g. \"192x192\"");
        }
    } else {
        failExpected("a height");
    }
    match(TokenKind::Semi);
}

void ParserImpl::parseOrigin(Asset& asset) {
    asset.originX = parseExpr();
    expect(TokenKind::Comma, "',' between origin coordinates");
    asset.originY = parseExpr();
    match(TokenKind::Semi);
}

InputDecl ParserImpl::parseInput() {
    advance(); // 'input'
    InputType type;
    switch (kind()) {
    case TokenKind::TypeColor: type = InputType::Color; break;
    case TokenKind::TypeF32: type = InputType::F32; break;
    case TokenKind::TypeU32: type = InputType::U32; break;
    case TokenKind::TypeI32: type = InputType::I32; break;
    default:
        failExpected("an input type (color, f32, u32 or i32)");
    }
    advance();
    const Token name = expectName("an input name");
    expect(TokenKind::Assign, "'=' and a default value");
    auto value = parseLiteralValue();
    match(TokenKind::Semi);
    return InputDecl{type, name.text, std::move(value), name.location};
}

// Input defaults allow parentheses and one unary minus, but no expression
// operators/calls. Numeric/type validation belongs to the constant resolver.
std::unique_ptr<Expr> ParserImpl::parseLiteralValue(bool allowMinus) {
    if (match(TokenKind::LParen)) {
        auto value = parseLiteralValue(allowMinus);
        expect(TokenKind::RParen, "')' after the default value");
        return value;
    }
    if (allowMinus && match(TokenKind::Minus)) {
        const auto at = tokens_[pos_ - 1].location;
        return std::make_unique<Expr>(Unary{Unary::Op::Neg, parseLiteralValue(false), at});
    }
    if (auto value = parseLiteral()) {
        return value;
    }
    failExpected("a literal default value (parentheses and one numeric unary minus are allowed)");
}

OutputDecl ParserImpl::parseOutput() {
    advance(); // 'output'
    const bool isList = match(TokenKind::KeywordList);
    OutputKind outputKind;
    switch (kind()) {
    case TokenKind::KindPoint: outputKind = OutputKind::Point; break;
    case TokenKind::KindRect: outputKind = OutputKind::Rect; break;
    case TokenKind::KindCircle: outputKind = OutputKind::Circle; break;
    default:
        failExpected("an output kind (point, rect or circle)");
    }
    advance();
    const Token name = expectName("an output name");
    match(TokenKind::Semi);
    return OutputDecl{outputKind, isList, name.text, name.location};
}

// `palette name = [ e, e, ... ]` — the 'palette' keyword was consumed by
// parseAsset. The element list allows a trailing comma and may be empty
// (the checker requires at least one element).
PaletteDecl ParserImpl::parsePalette() {
    const Token name = expectName("a palette name");
    expect(TokenKind::Assign, "'=' and an array literal");
    expect(TokenKind::LBracket, "'['");
    std::vector<std::unique_ptr<Expr>> elements;
    while (kind() != TokenKind::RBracket) {
        if (kind() == TokenKind::End) {
            failHere("unterminated palette (missing ']')");
        }
        elements.push_back(parseExpr());
        if (!match(TokenKind::Comma)) {
            break;
        }
        // Allow a trailing comma.
        if (kind() == TokenKind::RBracket) {
            break;
        }
    }
    expect(TokenKind::RBracket, "']'");
    match(TokenKind::Semi);
    return PaletteDecl{name.text, std::move(elements), name.location};
}

Function ParserImpl::parseFunction() {
    const Token name = advance(); // 'draw' keyword or identifier
    return parseFunctionAfterName(name);
}

Function ParserImpl::parseFunctionAfterName(const Token& name) {
    if (name.text == "draw") {
        if (kind() == TokenKind::LParen) {
            failHere("'draw' takes no declared parameters; use 'draw { ... }'");
        }
        if (kind() == TokenKind::Arrow) {
            failHere("'draw' cannot return a value; use 'draw { ... }'");
        }
        return Function{name.text, {}, {}, parseBlock(), name.location};
    }
    expect(TokenKind::LParen, "'(' after the function name");
    std::vector<Param> params;
    while (kind() != TokenKind::RParen) {
        const Token paramName = expectName("a parameter name");
        expect(TokenKind::Colon, "':' and a parameter type");
        const ParamType type = parseParamType();
        params.push_back(Param{paramName.text, type, paramName.location});
        if (!match(TokenKind::Comma)) {
            break;
        }
    }
    expect(TokenKind::RParen, "')'");
    std::optional<ParamType> returnType;
    if (match(TokenKind::Arrow)) {
        returnType = parseParamType();
    }
    return Function{name.text, std::move(params), returnType, parseBlock(),
                    name.location};
}

ParamType ParserImpl::parseParamType() {
    switch (kind()) {
    case TokenKind::TypeI32:
        advance();
        return ParamType::I32;
    case TokenKind::TypeF32:
        advance();
        return ParamType::F32;
    case TokenKind::TypeColor:
        advance();
        return ParamType::Color;
    case TokenKind::TypeVec2:
        advance();
        return ParamType::Vec2;
    case TokenKind::TypeU32:
        failHere("u32 is a uniform-only type; parameters are i32, f32, color or vec2");
    default:
        failExpected("a parameter type (i32, f32, color or vec2)");
    }
}

// ---- statements -------------------------------------------------------------

std::vector<Stmt> ParserImpl::parseBlock() {
    expect(TokenKind::LBrace, "'{'");
    std::vector<Stmt> body;
    while (kind() != TokenKind::RBrace) {
        if (kind() == TokenKind::End) {
            failHere("unterminated block (missing '}')");
        }
        // Semicolons are optional statement separators.
        if (match(TokenKind::Semi)) {
            continue;
        }
        body.push_back(parseStatement());
    }
    advance(); // '}'
    return body;
}

Stmt ParserImpl::parseStatement() {
    switch (kind()) {
    case TokenKind::KeywordIf:
        return parseIf();
    case TokenKind::KeywordWhile:
        return parseWhile();
    case TokenKind::KeywordFor:
        return parseFor();
    case TokenKind::KeywordBreak:
        return Stmt{BreakStmt{advance().location}};
    case TokenKind::KeywordContinue:
        return Stmt{ContinueStmt{advance().location}};
    case TokenKind::KeywordReturn:
        return parseReturn();
    default:
        // Identifiers and soft keywords start assignments and calls.
        if (isNameKind(kind())) {
            return parseIdentifierStatement();
        }
        failExpected("a statement");
    }
}

Stmt ParserImpl::parseIdentifierStatement() {
    const Token name = advance();
    if (match(TokenKind::Assign)) {
        return Stmt{VarDecl{name.text, parseExpr(), name.location}};
    }
    // Otherwise it must be a call (possibly through a member chain).
    auto expr = parsePostfixAfter(
        std::make_unique<Expr>(Identifier{name.text, name.location}));
    if (!std::holds_alternative<Call>(*expr)) {
        throw SourceError{name.location, "expression statement must be a call" + keywordSuggestion(name.text)};
    }
    return Stmt{ExprStmt{std::move(expr), name.location}};
}

Stmt ParserImpl::parseIf() {
    const SourceLocation at = advance().location; // 'if'
    expect(TokenKind::LParen, "'(' after 'if'");
    auto condition = parseExpr();
    expect(TokenKind::RParen, "')' after the condition");
    auto thenBranch = parseBlock();
    std::vector<Stmt> elseBranch;
    if (match(TokenKind::KeywordElse)) {
        if (kind() == TokenKind::KeywordIf) {
            elseBranch.push_back(parseIf());
        } else {
            elseBranch = parseBlock();
        }
    }
    return Stmt{IfStmt{std::move(condition), std::move(thenBranch),
                       std::move(elseBranch), at}};
}

Stmt ParserImpl::parseWhile() {
    const SourceLocation at = advance().location; // 'while'
    expect(TokenKind::LParen, "'(' after 'while'");
    auto condition = parseExpr();
    expect(TokenKind::RParen, "')' after the condition");
    return Stmt{WhileStmt{std::move(condition), parseBlock(), at}};
}

Stmt ParserImpl::parseFor() {
    const SourceLocation at = advance().location; // 'for'
    if (kind() != TokenKind::Identifier) {
        failExpected("the loop variable after 'for'");
    }
    const std::string variable = advance().text;
    expect(TokenKind::KeywordIn, "'in' after the loop variable");
    auto from = parseExpr();
    expect(TokenKind::DotDot, "'..' between the loop bounds");
    auto to = parseExpr();
    return Stmt{ForStmt{std::move(variable), std::move(from), std::move(to),
                        parseBlock(), at}};
}

Stmt ParserImpl::parseReturn() {
    const SourceLocation at = advance().location; // 'return'
    std::unique_ptr<Expr> value;
    if (kind() != TokenKind::RBrace && kind() != TokenKind::Semi) {
        value = parseExpr();
    }
    return Stmt{ReturnStmt{std::move(value), at}};
}

} // namespace detail

Program parse(std::string_view source) {
    return detail::ParserImpl(lex(source)).parseProgram();
}

Program parseTokens(const std::vector<Token>& tokens) {
    return detail::ParserImpl(tokens).parseProgram();
}

} // namespace iprop::ips
