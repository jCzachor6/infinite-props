#include "ips/DeclarationConstants.hpp"

#include <limits>

#include "ips/Literals.hpp"

namespace iprop::ips {
namespace {

[[noreturn]] void invalidConstant(const Expr& expression) {
    throw SourceError{locationOf(expression),
        "declaration constants must be literals, optionally parenthesized "
        "or with one numeric unary minus", {}, DiagnosticCode::DeclarationConstant};
}

ResolvedConstant resolve(const Expr& expression, bool integerAsFloat) {
    const Expr* leaf = &expression;
    bool negate = false;
    if (const auto* unary = std::get_if<Unary>(leaf)) {
        if (unary->op != Unary::Op::Neg) invalidConstant(expression);
        negate = true;
        leaf = unary->operand.get();
    }
    const auto* literal = std::get_if<Literal>(leaf);
    if (!literal) invalidConstant(expression);
    if (negate && literal->kind != Literal::Kind::Int &&
        literal->kind != Literal::Kind::Float) {
        throw SourceError{locationOf(expression),
            "declaration unary minus requires a numeric literal"};
    }

    ResolvedConstant result;
    result.kind = literal->kind;
    switch (literal->kind) {
    case Literal::Kind::Int: {
        u32 bits = 0;
        const bool integerParsed = parseIntBits(literal->text, bits);
        if (!integerParsed && !integerAsFloat)
            throw SourceError{literal->location, "invalid integer declaration literal"};
        if (integerParsed) {
            // parseIntBits has checked the same stoll conversion. F32
            // defaults also retain larger accepted integer spellings, for
            // which no int64 mathematical value can be represented.
            const i64 mathematical = std::stoll(literal->text, nullptr, 0);
            result.mathematicalInteger = negate ? -mathematical : mathematical;
        }
        if (integerAsFloat) {
            u32 floatBits = 0;
            if (!parseFloatBits(literal->text, floatBits))
                throw SourceError{literal->location, "invalid numeric declaration literal"};
            f32 value = 0.f;
            std::memcpy(&value, &floatBits, sizeof(floatBits));
            result.value = negate ? -value : value;
        } else {
            result.value = static_cast<i32>(negate ? 0u - bits : bits);
        }
        break;
    }
    case Literal::Kind::Float: {
        u32 bits = 0;
        if (!parseFloatBits(literal->text, bits))
            throw SourceError{literal->location, "invalid floating declaration literal"};
        f32 value = 0.f;
        std::memcpy(&value, &bits, sizeof(bits));
        result.value = negate ? -value : value;
        break;
    }
    case Literal::Kind::Color: {
        Color color;
        if (!parseColor(literal->text, color))
            throw SourceError{literal->location, "invalid color declaration literal"};
        result.value = color;
        break;
    }
    case Literal::Kind::True: result.value = i32{1}; break;
    case Literal::Kind::False: result.value = i32{0}; break;
    case Literal::Kind::String:
        throw SourceError{literal->location, "declaration constants cannot hold string elements"};
    }
    return result;
}

} // namespace

ResolvedConstant resolveDeclarationConstant(const Expr& expression) {
    return resolve(expression, false);
}

ResolvedConstant resolveInputDefault(const InputDecl& input) {
    auto result = resolve(*input.defaultValue, input.type == InputType::F32);
    const bool matches = [&]() {
        switch (input.type) {
        case InputType::Color: return result.kind == Literal::Kind::Color;
        case InputType::F32:
            return result.kind == Literal::Kind::Int || result.kind == Literal::Kind::Float;
        case InputType::U32:
        case InputType::I32: return result.kind == Literal::Kind::Int;
        }
        return false;
    }();
    if (!matches)
        throw SourceError{locationOf(*input.defaultValue),
            "default value of input '" + input.name + "' does not match its declared type"};
    if (input.type == InputType::U32 && *result.mathematicalInteger < 0)
        throw SourceError{locationOf(*input.defaultValue),
            "negative default of input u32 '" + input.name + "'; use input i32 for signed defaults",
            {}, DiagnosticCode::UnsignedDefaultNegative};
    if (input.type == InputType::I32 &&
        (*result.mathematicalInteger < std::numeric_limits<i32>::min() ||
         *result.mathematicalInteger > std::numeric_limits<i32>::max()))
        throw SourceError{locationOf(*input.defaultValue),
            "default of input i32 '" + input.name +
            "' must be in [-2147483648, 2147483647] before wrapping", {}, DiagnosticCode::SignedDefaultRange};
    return result;
}

} // namespace iprop::ips
