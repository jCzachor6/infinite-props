#include "ips/Parser.hpp"

#include <algorithm>
#include <string>
#include <utility>

#include "ips/ParserDetail.hpp"

namespace iprop::ips {
namespace detail {

ParseResult ParserImpl::parseRecoveringProgram(std::size_t limit) {
    ParseResult result;
    recovery_ = &result;
    diagnosticLimit_ = std::clamp(limit, std::size_t{1}, kMaxParseDiagnostics);
    Program program;
    bool hasAsset = false;
    try {
        while (kind() != TokenKind::End) {
            const auto start = pos_;
            try {
                if (kind() == TokenKind::KeywordImport && !hasAsset && program.materials.empty()) {
                    auto import = parseImport();
                    result.imports.push_back(import);
                    program.imports.push_back(std::move(import));
                } else if (kind() == TokenKind::Identifier && peek().text == "material" &&
                           kind(1) == TokenKind::Identifier && kind(2) == TokenKind::LBrace) {
                    program.materials.push_back(parseMaterialDef());
                } else if (kind() == TokenKind::Identifier && peek().text == "palette" &&
                           !hasAsset && kind(1) == TokenKind::Identifier) {
                    advance(); // 'palette'
                    program.materialPalettes.push_back(parsePalette());
                } else if (kind() == TokenKind::KeywordConst && !hasAsset) {
                    const auto constStart = pos_;
                    advance();
                    const auto type = parseParamType();
                    const Token name = expectName("a constant name");
                    expect(TokenKind::Assign, "'=' and a constant expression");
                    auto value = parseExpr();
                    match(TokenKind::Semi);
                    program.materialConstants.push_back(
                        {type, name.text, std::move(value), name.location, {constStart, pos_}});
                } else if (kind() == TokenKind::KeywordAsset && !hasAsset) {
                    program.asset = parseAsset();
                    hasAsset = true;
                } else if (kind() == TokenKind::KeywordAsset) {
                    failHere("multiple asset blocks in one file");
                } else if (hasAsset) {
                    failExpected("end of file");
                } else {
                    failExpected("an 'asset' block");
                }
            } catch (const SourceError& error) {
                recordError(error);
                synchronize(start, RecoveryScope::Program);
            }
        }
        // A failed declaration/header already explains an incomplete file.
        // Do not add an expected-asset cascade after synchronizing to EOF.
        if (hasAsset && !program.materials.empty() && result.diagnostics.empty()) {
            throw SourceError{program.materials.front().location,
                              "material definitions must be in a separate file without an asset"};
        }
        if (!hasAsset && program.materials.empty() && result.diagnostics.empty()) {
            failExpected("an 'asset' block");
        }
        program.isMaterialFile = !hasAsset && !program.materials.empty();
        if (result.diagnostics.empty()) {
            program.sourceTokens = std::move(tokens_);
            result.program = std::move(program);
        }
    } catch (const SourceError& error) {
        try {
            recordError(error);
        } catch (const RecoveryHalt&) {}
    } catch (const RecoveryHalt&) {}
    recovery_ = nullptr;
    return result;
}

void ParserImpl::recordError(SourceError error) {
    if (error.stage == DiagnosticStage::Unspecified) error.stage = DiagnosticStage::Parser;
    if (error.code == DiagnosticCode::Unspecified) error.code = defaultDiagnosticCode(error.stage);
    recovery_->diagnostics.push_back(std::move(error));
    if (recovery_->diagnostics.size() >= diagnosticLimit_) {
        recovery_->limitReached = true;
        throw RecoveryHalt{};
    }
    // Stop all enclosing constructs at EOF rather than reporting a missing
    // delimiter separately for each nested block.
    if (kind() == TokenKind::End) throw RecoveryHalt{};
}

bool ParserImpl::recoveryStart(RecoveryScope scope) const {
    if (scope == RecoveryScope::Program) {
        return kind() == TokenKind::KeywordImport || kind() == TokenKind::KeywordAsset;
    }
    if (scope == RecoveryScope::Declaration) {
        if (kind() == TokenKind::KeywordInput || kind() == TokenKind::KeywordOutput ||
            kind() == TokenKind::KeywordDraw || kind() == TokenKind::KeywordConst) return true;
        if (kind() == TokenKind::Identifier) {
            const auto& name = peek().text;
            if (name == "size" || name == "origin" || name == "palette") return true;
        }
        return isNameKind(kind()) && kind(1) == TokenKind::LParen;
    }
    switch (kind()) {
    case TokenKind::KeywordIf:
    case TokenKind::KeywordWhile:
    case TokenKind::KeywordFor:
    case TokenKind::KeywordReturn:
    case TokenKind::KeywordBreak:
    case TokenKind::KeywordContinue:
    case TokenKind::KeywordLayer:
    case TokenKind::KeywordMask:
    case TokenKind::KeywordClip:
    case TokenKind::KeywordSampler:
    case TokenKind::KeywordStepper:
    case TokenKind::TypeDistribution:
        return true;
    default:
        return isNameKind(kind()) &&
               (kind(1) == TokenKind::Assign || kind(1) == TokenKind::LParen ||
                kind(1) == TokenKind::Dot);
    }
}

void ParserImpl::synchronize(std::size_t start, RecoveryScope scope) {
    // Include delimiters consumed by the failed production. A declaration
    // inside a discarded function, or a named argument inside a failed call,
    // must never be mistaken for a sibling declaration/statement.
    int braces = 0;
    int parens = 0;
    int brackets = 0;
    bool skippingElseArm = false;
    const auto update = [&](TokenKind token) {
        switch (token) {
        case TokenKind::LBrace: ++braces; break;
        case TokenKind::RBrace: if (braces > 0) --braces; break;
        case TokenKind::LParen: ++parens; break;
        case TokenKind::RParen: if (parens > 0) --parens; break;
        case TokenKind::LBracket: ++brackets; break;
        case TokenKind::RBracket: if (brackets > 0) --brackets; break;
        default: break;
        }
    };
    for (auto at = start; at < pos_; ++at) update(tokens_[at].kind);
    while (kind() != TokenKind::End) {
        if (braces == 0) {
            // Preserve the delimiter owned by parseAsset/parseBlock even if
            // the failed expression left a parenthesis/bracket unclosed.
            if (kind() == TokenKind::RBrace && scope != RecoveryScope::Program) return;
            // ';' cannot occur in an expression: it safely terminates a
            // failed call/index even when its closing delimiter is missing.
            if (match(TokenKind::Semi)) return;
            if (!skippingElseArm && pos_ > start && parens == 0 && brackets == 0 &&
                recoveryStart(scope)) return;
        }
        const bool closesNestedBody = kind() == TokenKind::RBrace && braces == 1;
        update(advance().kind);
        if (closesNestedBody) {
            // Discard an else arm attached to a failed conditional together
            // with that conditional, rather than reporting it as a sibling.
            if (scope == RecoveryScope::Statement && kind() == TokenKind::KeywordElse) {
                advance();
                skippingElseArm = true;
                parens = 0;
                brackets = 0;
                continue;
            }
            return;
        }
    }
}

// ---- declarations -----------------------------------------------------------

Import ParserImpl::parseImport() {
    const auto start = pos_;
    const SourceLocation at = advance().location; // 'import'
    const Token alias = expectName("an import alias");
    expect(TokenKind::KeywordFrom, "'from'");
    const Token path = expect(TokenKind::String, "a quoted file path");
    match(TokenKind::Semi);
    return Import{alias.text, path.text, at, {start, pos_}};
}

MaterialDef ParserImpl::parseMaterialDef() {
    const auto start = pos_;
    const SourceLocation at = advance().location; // 'material'
    const Token name = expect(TokenKind::Identifier, "a material name");
    expect(TokenKind::LBrace, "'{'");
    MaterialDef def;
    def.name = name.text;
    def.location = at;
    while (kind() != TokenKind::RBrace) {
        if (kind() == TokenKind::End) failHere("unterminated material block (missing '}')");
        if (match(TokenKind::Semi)) continue;
        const Token field = expectName("a material field name");
        expect(TokenKind::Assign, "'=' and a field value");
        auto value = parseExpr();
        match(TokenKind::Semi);
        def.fields.push_back(MaterialField{field.text, std::move(value), field.location});
    }
    advance(); // '}'
    def.sourceRange = {start, pos_};
    return def;
}

Asset ParserImpl::parseAsset() {
    const auto assetStart = pos_;
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
        const auto start = pos_;
        try {
            parseDeclaration(asset);
        } catch (const SourceError& error) {
            recordError(error);
            synchronize(start, RecoveryScope::Declaration);
        }
    }
    advance(); // '}'
    asset.sourceRange = {assetStart, pos_};
    return asset;
}

void ParserImpl::parseDeclaration(Asset& asset) {
    const auto start = pos_;
    switch (kind()) {
    case TokenKind::KeywordInput:
        asset.inputs.push_back(parseInput());
        asset.inputs.back().sourceRange = {start, pos_};
        break;
    case TokenKind::KeywordOutput:
        asset.outputs.push_back(parseOutput());
        asset.outputs.back().sourceRange = {start, pos_};
        break;
    case TokenKind::KeywordConst: {
        advance();
        const auto type = parseParamType();
        const Token name = expectName("a constant name");
        expect(TokenKind::Assign, "'=' and a constant expression");
        auto value = parseExpr();
        match(TokenKind::Semi);
        asset.constants.push_back({type, name.text, std::move(value), name.location, {start, pos_}});
        break;
    }
    case TokenKind::KeywordDraw:
        asset.functions.push_back(parseFunction());
        asset.functions.back().sourceRange = {start, pos_};
        break;
    case TokenKind::Identifier: {
        const Token declaration = advance();
        if (declaration.text == "size") {
            parseSize(asset);
            asset.metadataSyntax.push_back({MetadataSyntax::Kind::Size, {start, pos_}});
        } else if (declaration.text == "origin") {
            parseOrigin(asset);
            asset.metadataSyntax.push_back({MetadataSyntax::Kind::Origin, {start, pos_}});
        } else if (declaration.text == "palette") {
            asset.palettes.push_back(parsePalette());
            asset.palettes.back().sourceRange = {start, pos_};
        } else {
            const auto hint = keywordSuggestion(declaration.text);
            if (kind() != TokenKind::LParen && !hint.empty())
                throw SourceError{declaration.location, "unknown asset declaration '" + declaration.text + "'" + hint};
            asset.functions.push_back(parseFunctionAfterName(declaration));
            asset.functions.back().sourceRange = {start, pos_};
        }
        break;
    }
    default:
        // Soft keywords may also name a function.
        if (isNameKind(kind())) {
            asset.functions.push_back(parseFunctionAfterName(advance()));
            asset.functions.back().sourceRange = {start, pos_};
        } else {
            failExpected("a declaration inside the asset block");
        }
    }
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
    const auto typeLocation=peek().location;
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
    return InputDecl{type, name.text, std::move(value), name.location,typeLocation};
}

// Input defaults allow parentheses and one unary minus, but no expression
// operators/calls. Numeric/type validation belongs to the constant resolver.
std::unique_ptr<Expr> ParserImpl::parseLiteralValue(bool allowMinus) {
    const auto start = pos_;
    if (match(TokenKind::LParen)) {
        auto value = parseLiteralValue(allowMinus);
        expect(TokenKind::RParen, "')' after the default value");
        value->sourceRange = {start, pos_};
        return value;
    }
    if (allowMinus && match(TokenKind::Minus)) {
        const auto at = tokens_[pos_ - 1].location;
        auto value = std::make_unique<Expr>(Unary{Unary::Op::Neg, parseLiteralValue(false), at});
        value->sourceRange = value->sourceCoreRange = {start, pos_};
        return value;
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
    case TokenKind::KeywordMask: outputKind = OutputKind::Mask; break;
    case TokenKind::KeywordLayer: outputKind = OutputKind::Layer; break;
    default:
        failExpected("an output kind (point, rect, circle, mask or layer)");
    }
    if (isList && isResourceOutput(outputKind)) failHere("resource exports cannot be lists");
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
        if (kind() == TokenKind::Identifier && !peek().text.empty() &&
            peek().text.front() >= 'A' && peek().text.front() <= 'Z') {
            const Token material = advance();
            params.push_back(Param{paramName.text, ParamType::I32, paramName.location, material.text});
        } else {
            const ParamType type = parseParamType();
            params.push_back(Param{paramName.text, type, paramName.location});
        }
        if (!match(TokenKind::Comma)) {
            break;
        }
    }
    expect(TokenKind::RParen, "')'");
    std::optional<ParamType> returnType;
    if (match(TokenKind::Arrow)) {
        if (kind() == TokenKind::Identifier)
            failHere("returning a material is not supported; return a field such as m.base");
        returnType = parseParamType();
    }
    return Function{name.text, std::move(params), returnType, parseBlock(),
                    name.location};
}

ParamType ParserImpl::parseParamType(bool insideList) {
    if (kind() == TokenKind::KeywordList && kind(1) == TokenKind::Less) {
        advance(); advance();
        if (kind() == TokenKind::KeywordList) failHere("nested lists are not supported");
        const auto item = parseParamType(true);
        if (!item.isScalar() && !item.isDescription()) failHere("lists of surfaces are not supported");
        if (listTypeClosed_) listTypeClosed_ = false;
        else expect(TokenKind::Greater, "'>' after list element type");
        return ParamType::list(item);
    }
    if ((kind() == TokenKind::KeywordSampler || kind() == TokenKind::KeywordStepper) && kind(1) == TokenKind::Less) {
        const bool stepper = kind() == TokenKind::KeywordStepper;
        const std::string name = stepper ? "stepper" : "sampler";
        advance(); advance();
        if (kind() == TokenKind::KeywordList || kind() == TokenKind::KeywordSampler || kind() == TokenKind::KeywordStepper)
            failHere(name + "<T> supports only i32, f32, color or vec2");
        const auto item = parseParamType();
        if (!item.isScalar() || item == ParamType::Distribution)
            failHere(name + "<T> supports only i32, f32, color or vec2");
        if (insideList && match(TokenKind::Shr)) listTypeClosed_ = true;
        else expect(TokenKind::Greater, "'>' after description element type");
        return stepper ? ParamType::stepper(item) : ParamType::sampler(item);
    }
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
        failHere("u32 is a uniform-only type; parameters are i32, f32, color, vec2 or distribution");
    case TokenKind::TypeDistribution:
        advance();
        return ParamType::Distribution;
    case TokenKind::KeywordLayer:
        advance();
        return ParamType::Layer;
    case TokenKind::KeywordMask:
        advance();
        return ParamType::Mask;
    default:
        failExpected("a parameter type (i32, f32, color, vec2, distribution, list<T>, sampler<T>, stepper<T>, layer or mask)");
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
        const auto start = pos_;
        try {
            auto statement = parseStatement();
            match(TokenKind::Semi);
            statement.sourceRange = {start, pos_};
            body.push_back(std::move(statement));
        } catch (const SourceError& error) {
            recordError(error);
            synchronize(start, RecoveryScope::Statement);
        }
    }
    advance(); // '}'
    return body;
}

Stmt ParserImpl::parseStatement() {
    switch (kind()) {
    case TokenKind::KeywordLayer:
        return parseLayer();
    case TokenKind::KeywordClip: {
        const auto at = advance().location;
        expect(TokenKind::LParen, "'(' after clip");
        auto mask = parseExpr();
        expect(TokenKind::RParen, "')' after clip mask");
        return Stmt{ClipStmt{std::move(mask), parseBlock(), at}};
    }
    case TokenKind::KeywordMask:
    case TokenKind::TypeDistribution:
    case TokenKind::KeywordSampler:
    case TokenKind::KeywordStepper:
        return parseIdentifierStatement();
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
    const auto start = pos_;
    if (kind() == TokenKind::KeywordMask) {
        const auto at = peek().location;
        if (kind(1) == TokenKind::Dot)
            return Stmt{ExprStmt{parseExpr(), at}};
        advance();
        const auto local = expectName("a local mask name");
        expect(TokenKind::Assign, "'=' after local mask name");
        return Stmt{VarDecl{local.text, parseExpr(), local.location, ParamType::Mask}};
    }
    if (((kind() == TokenKind::KeywordList) ||
         kind() == TokenKind::KeywordSampler || kind() == TokenKind::KeywordStepper) && kind(1) == TokenKind::Less &&
        (kind(2) == TokenKind::TypeI32 || kind(2) == TokenKind::TypeF32 ||
         kind(2) == TokenKind::TypeColor || kind(2) == TokenKind::TypeVec2 ||
         kind(2) == TokenKind::TypeDistribution || kind(2) == TokenKind::KeywordSampler || kind(2) == TokenKind::KeywordStepper)) {
        const auto type = parseParamType();
        const auto local = expectName("a local list name");
        expect(TokenKind::Assign, "'=' after local list name");
        return Stmt{VarDecl{local.text, parseExpr(), local.location, type}};
    }
    if (kind() == TokenKind::TypeDistribution) {
        advance();
        const auto local = expectName("a local distribution name");
        expect(TokenKind::Assign, "'=' after local distribution name");
        return Stmt{VarDecl{local.text, parseExpr(), local.location, ParamType::Distribution}};
    }
    if (kind() == TokenKind::KeywordSampler || kind() == TokenKind::KeywordStepper) {
        const auto at = peek().location;
        return Stmt{ExprStmt{parseExpr(), at}};
    }
    const Token name = advance();
    if (match(TokenKind::Assign)) {
        return Stmt{VarDecl{name.text, parseExpr(), name.location}};
    }
    // Otherwise it must be a call (possibly through a member chain).
    auto primary = std::make_unique<Expr>(Identifier{name.text, name.location});
    primary->sourceRange = primary->sourceCoreRange = {start, pos_};
    auto expr = parsePostfixAfter(std::move(primary));
    if (const auto* member = std::get_if<Member>(expr.get()); member && kind() == TokenKind::Assign) {
        throw SourceError{member->location, "cannot assign to '." + member->name +
            "': members are read-only; assign a whole new value instead, such as "
            "rgba(...) for a color or {x, y} for a vec2"};
    }
    if (std::holds_alternative<Index>(*expr) && kind() == TokenKind::Assign)
        throw SourceError{locationOf(*expr), "list elements are immutable; assign a whole new list instead"};
    if (!std::holds_alternative<Call>(*expr) && !std::holds_alternative<SamplerPick>(*expr) &&
        !std::holds_alternative<SamplerCapture>(*expr) && !std::holds_alternative<ListMethod>(*expr) &&
        !std::holds_alternative<SurfaceBlur>(*expr)) {
        throw SourceError{name.location, "expression statement must be a call" + keywordSuggestion(name.text)};
    }
    return Stmt{ExprStmt{std::move(expr), name.location}};
}

Stmt ParserImpl::parseLayer() {
    const auto at = advance().location;
    const auto local = expectName("a layer result name");
    if (match(TokenKind::Assign))
        return Stmt{VarDecl{local.text, parseExpr(), local.location, ParamType::Layer}};
    auto alpha = CompositionMode::Inherited;
    if (match(TokenKind::LParen)) {
        const auto option = expectName("'alpha'");
        if (option.text != "alpha") throw SourceError{option.location, "layer supports only the 'alpha' option"};
        expect(TokenKind::Assign, "'=' after alpha");
        const auto base = expectName("'surface'");
        expect(TokenKind::Dot, "'.' in surface alpha mode");
        const auto mode = expectName("'legacy' or 'source_over'");
        const auto* spec = compositionSpec(mode.text);
        if (base.text != "surface" || !spec)
            throw SourceError{mode.location, "layer alpha must be surface.legacy or surface.source_over"};
        alpha = spec->mode;
        expect(TokenKind::RParen, "')' after layer alpha");
    }
    return Stmt{LayerStmt{local.text, alpha, parseBlock(), at}};
}

Stmt ParserImpl::parseIf() {
    const SourceLocation at = advance().location; // 'if'
    expect(TokenKind::LParen, "'(' after 'if'");
    auto condition = parseExpr();
    expect(TokenKind::RParen, "')' after the condition");
    auto thenBranch = parseBlock();
    std::vector<Stmt> elseBranch;
    TokenRange elseRange;
    if (match(TokenKind::KeywordElse)) {
        elseRange.begin = pos_ - 1;
        if (kind() == TokenKind::KeywordIf) {
            const auto start = pos_;
            auto branch = parseIf();
            branch.sourceRange = {start, pos_};
            elseBranch.push_back(std::move(branch));
        } else {
            elseBranch = parseBlock();
        }
        elseRange.end = pos_;
    }
    return Stmt{IfStmt{std::move(condition), std::move(thenBranch),
                       std::move(elseBranch), at, elseRange}};
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
    auto result = parseRecovering(source, 1);
    if (!result.valid()) throw result.diagnostics.front();
    return std::move(*result.program);
}

ParseResult parseRecovering(std::string_view source, std::size_t limit) {
    std::vector<Token> tokens;
    try {
        tokens = lex(source);
    } catch (const SourceError& caught) {
        auto error = caught;
        if (error.stage == DiagnosticStage::Unspecified) error.stage = DiagnosticStage::Lexer;
        if (error.code == DiagnosticCode::Unspecified) error.code = defaultDiagnosticCode(error.stage);
        ParseResult result;
        result.diagnostics.push_back(std::move(error));
        result.limitReached = std::clamp(limit, std::size_t{1}, kMaxParseDiagnostics) == 1;
        return result;
    }
    return detail::ParserImpl(std::move(tokens)).parseRecoveringProgram(limit);
}

} // namespace iprop::ips
