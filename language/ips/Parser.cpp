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
    advance(); // 'import'
    const Token alias = expectName("an import alias");
    expect(TokenKind::KeywordFrom, "'from'");
    const Token path = expect(TokenKind::String, "a quoted file path");
    return Import{alias.text, path.text};
}

Asset ParserImpl::parseAsset() {
    advance(); // 'asset'
    const Token name = expect(TokenKind::Identifier, "an asset name");
    expect(TokenKind::LBrace, "'{'");
    Asset asset;
    asset.name = name.text;

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
            } else if (declaration.text == "archetypes") {
                asset.archetypes =
                    toU32(expect(TokenKind::Int, "an archetype count"),
                          "archetype count");
            } else {
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
}

void ParserImpl::parseOrigin(Asset& asset) {
    asset.originX = parseExpr();
    expect(TokenKind::Comma, "',' between origin coordinates");
    asset.originY = parseExpr();
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
    return InputDecl{type, name.text, parseLiteralValue()};
}

// The default value of an input is a literal; type checking happens in the
// semantic phase.
std::unique_ptr<Expr> ParserImpl::parseLiteralValue() {
    if (auto value = parseLiteral()) {
        return value;
    }
    failExpected("a literal default value");
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
    return OutputDecl{outputKind, isList, name.text};
}

Function ParserImpl::parseFunction() {
    const Token name = advance(); // 'draw' keyword or identifier
    return parseFunctionAfterName(name);
}

Function ParserImpl::parseFunctionAfterName(const Token& name) {
    expect(TokenKind::LParen, "'(' after the function name");
    std::vector<std::string> params;
    while (kind() != TokenKind::RParen) {
        params.push_back(expectName("a parameter name").text);
        if (!match(TokenKind::Comma)) {
            break;
        }
    }
    expect(TokenKind::RParen, "')'");
    return Function{name.text, std::move(params), parseBlock()};
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
        return Stmt{VarDecl{name.text, parseExpr()}};
    }
    // Otherwise it must be a call (possibly through a member chain).
    auto expr = parsePostfixAfter(std::make_unique<Expr>(Identifier{name.text}));
    if (!std::holds_alternative<Call>(*expr)) {
        throw SourceError{name.location, "expression statement must be a call"};
    }
    return Stmt{ExprStmt{std::move(expr)}};
}

Stmt ParserImpl::parseIf() {
    advance(); // 'if'
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
                       std::move(elseBranch)}};
}

Stmt ParserImpl::parseWhile() {
    advance(); // 'while'
    expect(TokenKind::LParen, "'(' after 'while'");
    auto condition = parseExpr();
    expect(TokenKind::RParen, "')' after the condition");
    return Stmt{WhileStmt{std::move(condition), parseBlock()}};
}

Stmt ParserImpl::parseReturn() {
    advance(); // 'return'
    std::unique_ptr<Expr> value;
    if (kind() != TokenKind::RBrace && kind() != TokenKind::Semi) {
        value = parseExpr();
    }
    return Stmt{ReturnStmt{std::move(value)}};
}

} // namespace detail

Program parse(std::string_view source) {
    return detail::ParserImpl(lex(source)).parseProgram();
}

Program parseTokens(const std::vector<Token>& tokens) {
    return detail::ParserImpl(tokens).parseProgram();
}

} // namespace iprop::ips
