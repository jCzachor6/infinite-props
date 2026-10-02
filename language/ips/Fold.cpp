#include "ips/Fold.hpp"

#include "ips/Literals.hpp"

namespace iprop::ips {
namespace {

bool foldMixed(ips::Binary::Op op, f32 a, f32 b, Value& out) {
    switch (op) {
    case ips::Binary::Op::Add: out = a + b; return true;
    case ips::Binary::Op::Sub: out = a - b; return true;
    case ips::Binary::Op::Mul: out = a * b; return true;
    case ips::Binary::Op::Div: out = a / b; return true;
    case ips::Binary::Op::Eq: out = static_cast<i32>(a == b ? 1 : 0); return true;
    case ips::Binary::Op::Ne: out = static_cast<i32>(a != b ? 1 : 0); return true;
    case ips::Binary::Op::Lt: out = static_cast<i32>(a < b ? 1 : 0); return true;
    case ips::Binary::Op::Le: out = static_cast<i32>(a <= b ? 1 : 0); return true;
    case ips::Binary::Op::Gt: out = static_cast<i32>(a > b ? 1 : 0); return true;
    case ips::Binary::Op::Ge: out = static_cast<i32>(a >= b ? 1 : 0); return true;
    default: return false;
    }
}

} // namespace

bool ConstantFolder::evaluate(const ips::Expr& expr, Value& out) {
    using namespace ips;
    if (const auto* lit = std::get_if<Literal>(&expr)) {
        switch (lit->kind) {
        case Literal::Kind::Int: {
            u32 bits = 0;
            if (!parseIntBits(lit->text, bits)) {
                return false;
            }
            out = static_cast<i32>(bits);
            return true;
        }
        case Literal::Kind::True:
            out = static_cast<i32>(1);
            return true;
        case Literal::Kind::False:
            out = static_cast<i32>(0);
            return true;
        case Literal::Kind::Float: {
            u32 bits = 0;
            if (!parseFloatBits(lit->text, bits)) {
                return false;
            }
            f32 value = 0.f;
            std::memcpy(&value, &bits, sizeof(value));
            out = value;
            return true;
        }
        case Literal::Kind::Color: {
            Color color;
            if (!parseColor(lit->text, color)) {
                return false;
            }
            out = color;
            return true;
        }
        case Literal::Kind::String:
            return false;
        }
    }
    if (const auto* un = std::get_if<Unary>(&expr)) {
        Value operand;
        if (!fold(*un->operand, operand)) {
            return false;
        }
        if (const auto* i = std::get_if<i32>(&operand)) {
            switch (un->op) {
            case Unary::Op::Neg:
                out = static_cast<i32>(0u - static_cast<u32>(*i));
                return true;
            case Unary::Op::Not:
                out = static_cast<i32>(*i == 0 ? 1 : 0);
                return true;
            case Unary::Op::BitNot:
                out = static_cast<i32>(~static_cast<u32>(*i));
                return true;
            }
        }
        if (const auto* f = std::get_if<f32>(&operand)) {
            if (un->op == Unary::Op::Neg) {
                out = -*f;
                return true;
            }
        }
        return false;
    }
    if (const auto* bin = std::get_if<Binary>(&expr)) {
        Value left;
        Value right;
        if (!fold(*bin->left, left) || !fold(*bin->right, right)) {
            return false;
        }
        const auto* li = std::get_if<i32>(&left);
        const auto* ri = std::get_if<i32>(&right);
        const auto* lf = std::get_if<f32>(&left);
        const auto* rf = std::get_if<f32>(&right);

        if (li != nullptr && ri != nullptr) {
            const u32 a = static_cast<u32>(*li);
            const u32 b = static_cast<u32>(*ri);
            switch (bin->op) {
            case Binary::Op::Add: out = static_cast<i32>(a + b); return true;
            case Binary::Op::Sub: out = static_cast<i32>(a - b); return true;
            case Binary::Op::Mul: out = static_cast<i32>(a * b); return true;
            case Binary::Op::Eq: out = static_cast<i32>(*li == *ri ? 1 : 0); return true;
            case Binary::Op::Ne: out = static_cast<i32>(*li != *ri ? 1 : 0); return true;
            case Binary::Op::Lt: out = static_cast<i32>(*li < *ri ? 1 : 0); return true;
            case Binary::Op::Le: out = static_cast<i32>(*li <= *ri ? 1 : 0); return true;
            case Binary::Op::Gt: out = static_cast<i32>(*li > *ri ? 1 : 0); return true;
            case Binary::Op::Ge: out = static_cast<i32>(*li >= *ri ? 1 : 0); return true;
            case Binary::Op::And:
                out = static_cast<i32>((*li != 0 && *ri != 0) ? 1 : 0);
                return true;
            case Binary::Op::Or:
                out = static_cast<i32>((*li != 0 || *ri != 0) ? 1 : 0);
                return true;
            case Binary::Op::Xor: out = static_cast<i32>(a ^ b); return true;
            case Binary::Op::BitAnd: out = static_cast<i32>(a & b); return true;
            case Binary::Op::BitOr: out = static_cast<i32>(a | b); return true;
            case Binary::Op::Shl: out = static_cast<i32>(a << (b & 31)); return true;
            case Binary::Op::Shr: out = static_cast<i32>(a >> (b & 31)); return true;
            default: return false; // div/mod: keep the runtime trap
            }
        }
        if (lf != nullptr && rf != nullptr) {
            switch (bin->op) {
            case Binary::Op::Add: out = *lf + *rf; return true;
            case Binary::Op::Sub: out = *lf - *rf; return true;
            case Binary::Op::Mul: out = *lf * *rf; return true;
            case Binary::Op::Div: out = *lf / *rf; return true;
            case Binary::Op::Eq: out = static_cast<i32>(*lf == *rf ? 1 : 0); return true;
            case Binary::Op::Ne: out = static_cast<i32>(*lf != *rf ? 1 : 0); return true;
            case Binary::Op::Lt: out = static_cast<i32>(*lf < *rf ? 1 : 0); return true;
            case Binary::Op::Le: out = static_cast<i32>(*lf <= *rf ? 1 : 0); return true;
            case Binary::Op::Gt: out = static_cast<i32>(*lf > *rf ? 1 : 0); return true;
            case Binary::Op::Ge: out = static_cast<i32>(*lf >= *rf ? 1 : 0); return true;
            default: return false;
            }
        }
        if (lf != nullptr && ri != nullptr) {
            return foldMixed(bin->op, *lf, static_cast<f32>(*ri), out);
        }
        if (li != nullptr && rf != nullptr) {
            return foldMixed(bin->op, static_cast<f32>(*li), *rf, out);
        }
        return false;
    }
    return false;
}

bool ConstantFolder::fold(const Expr& expr, Value& out) {
    auto found = values_.find(&expr);
    if (found == values_.end()) {
        Value value;
        const auto result = evaluate(expr, value) ? std::optional<Value>(value) : std::nullopt;
        found = values_.emplace(&expr, result).first;
    }
    if (!found->second) return false;
    out = *found->second;
    return true;
}

bool foldExpression(const ips::Expr& expr, Value& out) {
    return ConstantFolder{}.fold(expr, out);
}

} // namespace iprop::ips
