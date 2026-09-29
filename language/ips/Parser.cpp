#include "ips/Parser.hpp"

#include <cstdlib>
#include <string>
#include <utility>

namespace iprop::ips {
namespace {

// Converts a numeric token to a u32, rejecting values out of range.
std::uint32_t toU32(const Token& token, const char* what) {
    char* end = nullptr;
    const unsigned long long value = std::strtoull(token.text.c_str(), &end, 0);
    if (end == token.text.c_str() || *end != '\0' || value > 0xFFFFFFFFull) {
        throw SourceError{token.location,
                          std::string(what) + " out of range: " + token.text};
    }
    return static_cast<std::uint32_t>(value);
}

bool isDigits(std::string_view text) {
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

// "Soft" keywords may appear wherever a name is expected: `hair.draw(...)`,
// `at = {32, 4}`, `input color color = #33291f`. Hard keywords (control
// flow and declaration introducers) stay reserved.
bool isNameKind(TokenKind tokenKind) {
    switch (tokenKind) {
    case TokenKind::Identifier:
    case TokenKind::KeywordDraw:
    case TokenKind::KeywordAt:
    case TokenKind::TypeColor:
    case TokenKind::TypeF32:
    case TokenKind::TypeU32:
    case TokenKind::TypeI32:
    case TokenKind::KindPoint:
    case TokenKind::KindRect:
    case TokenKind::KindCircle:
        return true;
    default:
        return false;
    }
}

class ParserImpl {
public:
    explicit ParserImpl(std::vector<Token> tokens) : tokens_(std::move(tokens)) {}

    Program parseProgram() {
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

private:
    std::vector<Token> tokens_;
    std::size_t pos_ = 0;

    // ---- token helpers ----------------------------------------------------

    const Token& peek(std::size_t ahead = 0) const {
        if (tokens_.empty()) {
            static const Token kEnd{};
            return kEnd;
        }
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
            failExpected(what);
        }
        return advance();
    }

    const Token& expectName(const char* what) {
        if (!isNameKind(kind())) {
            failExpected(what);
        }
        return advance();
    }

    [[noreturn]] void failExpected(const char* what) {
        std::string message = "expected ";
        message += what;
        message += ", got ";
        message += tokenName(kind());
        failHere(std::move(message));
    }

    [[noreturn]] void failHere(std::string message) {
        throw SourceError{peek().location, std::move(message)};
    }

    // ---- top level ---------------------------------------------------------

    Import parseImport() {
        advance(); // 'import'
        const Token alias = expectName("an import alias");
        expect(TokenKind::KeywordFrom, "'from'");
        const Token path = expect(TokenKind::String, "a quoted file path");
        return Import{alias.text, path.text};
    }

    Asset parseAsset() {
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

    void parseSize(Asset& asset) {
        const Token width = expect(TokenKind::Int, "a width");
        asset.sizeX = toU32(width, "width");

        // "192x192" lexes as Int "192" then Identifier "x192".
        if (kind() == TokenKind::Int) {
            asset.sizeY = toU32(advance(), "height");
        } else if (kind() == TokenKind::Identifier) {
            const Token height = advance();
            if (height.text.size() > 1 && height.text[0] == 'x' &&
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

    void parseOrigin(Asset& asset) {
        asset.originX = parseExpr();
        expect(TokenKind::Comma, "',' between origin coordinates");
        asset.originY = parseExpr();
    }

    InputDecl parseInput() {
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

    // The default value of an input is a literal; type checking happens in
    // the semantic phase.
    std::unique_ptr<Expr> parseLiteralValue() {
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
            failExpected("a literal default value");
        }
    }

    OutputDecl parseOutput() {
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

    Function parseFunction() {
        const Token name = advance(); // 'draw' keyword or identifier
        return parseFunctionAfterName(name);
    }

    Function parseFunctionAfterName(const Token& name) {
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

    // ---- statements ---------------------------------------------------------

    std::vector<Stmt> parseBlock() {
        expect(TokenKind::LBrace, "'{'");
        std::vector<Stmt> body;
        while (kind() != TokenKind::RBrace) {
            if (kind() == TokenKind::End) {
                failHere("unterminated block (missing '}')");
            }
            body.push_back(parseStatement());
        }
        advance(); // '}'
        return body;
    }

    Stmt parseStatement() {
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

    Stmt parseIdentifierStatement() {
        const Token name = advance();
        if (match(TokenKind::Assign)) {
            return Stmt{VarDecl{name.text, parseExpr()}};
        }
        // Otherwise it must be a call (possibly through a member chain).
        auto expr = parsePostfixAfter(std::make_unique<Expr>(Identifier{name.text}));
        if (!std::holds_alternative<Call>(*expr)) {
            throw SourceError{name.location,
                              "expression statement must be a call"};
        }
        return Stmt{ExprStmt{std::move(expr)}};
    }

    Stmt parseIf() {
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

    Stmt parseWhile() {
        advance(); // 'while'
        expect(TokenKind::LParen, "'(' after 'while'");
        auto condition = parseExpr();
        expect(TokenKind::RParen, "')' after the condition");
        return Stmt{WhileStmt{std::move(condition), parseBlock()}};
    }

    Stmt parseReturn() {
        advance(); // 'return'
        std::unique_ptr<Expr> value;
        if (kind() != TokenKind::RBrace) {
            value = parseExpr();
        }
        return Stmt{ReturnStmt{std::move(value)}};
    }

    // ---- expressions ---------------------------------------------------------

    std::unique_ptr<Expr> parseExpr() { return parseOr(); }

    std::unique_ptr<Expr> parseOr() {
        auto left = parseAnd();
        while (match(TokenKind::OrOr)) {
            left = binary(Binary::Op::Or, std::move(left), parseAnd());
        }
        return left;
    }

    std::unique_ptr<Expr> parseAnd() {
        auto left = parseBitOr();
        while (match(TokenKind::AndAnd)) {
            left = binary(Binary::Op::And, std::move(left), parseBitOr());
        }
        return left;
    }

    std::unique_ptr<Expr> parseBitOr() {
        auto left = parseBitXor();
        while (match(TokenKind::Pipe)) {
            left = binary(Binary::Op::BitOr, std::move(left), parseBitXor());
        }
        return left;
    }

    std::unique_ptr<Expr> parseBitXor() {
        auto left = parseBitAnd();
        while (match(TokenKind::Caret)) {
            left = binary(Binary::Op::Xor, std::move(left), parseBitAnd());
        }
        return left;
    }

    std::unique_ptr<Expr> parseBitAnd() {
        auto left = parseEquality();
        while (match(TokenKind::Amp)) {
            left = binary(Binary::Op::BitAnd, std::move(left), parseEquality());
        }
        return left;
    }

    std::unique_ptr<Expr> parseEquality() {
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

    std::unique_ptr<Expr> parseComparison() {
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

    std::unique_ptr<Expr> parseAdditive() {
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

    std::unique_ptr<Expr> parseMultiplicative() {
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

    std::unique_ptr<Expr> parseUnary() {
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

    std::unique_ptr<Expr> parsePostfix() { return parsePostfixAfter(parsePrimary()); }

    // Continues a postfix chain on an already-parsed primary: member access
    // ('.' name) and calls ('(' args ')').
    std::unique_ptr<Expr> parsePostfixAfter(std::unique_ptr<Expr> expr) {
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

    std::unique_ptr<Expr> parsePrimary() {
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

    std::unique_ptr<Expr> parseVector() {
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

    std::vector<Arg> parseArgs() {
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

    // ---- small constructors ---------------------------------------------------

    static std::unique_ptr<Expr> literal(Literal::Kind literalKind,
                                         std::string text) {
        return std::make_unique<Expr>(Literal{literalKind, std::move(text)});
    }

    static std::unique_ptr<Expr> binary(Binary::Op op,
                                        std::unique_ptr<Expr> left,
                                        std::unique_ptr<Expr> right) {
        return std::make_unique<Expr>(
            Binary{op, std::move(left), std::move(right)});
    }
};

} // namespace

Program parse(std::string_view source) { return ParserImpl(lex(source)).parseProgram(); }

Program parseTokens(const std::vector<Token>& tokens) {
    return ParserImpl(tokens).parseProgram();
}

} // namespace iprop::ips
