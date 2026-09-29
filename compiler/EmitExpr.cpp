#include "Emit.hpp"

#include <utility>

#include "Fold.hpp"

namespace iprop::compiler {

// ---- expressions -------------------------------------------------------------

ips::ValueType Emitter::emitExpr(const ips::Expr& expr) {
    currentLine_ = static_cast<u32>(ips::locationOf(expr).line);
    if (Value folded; foldExpression(expr, folded)) {
        emitConst(folded);
        if (std::holds_alternative<i32>(folded)) {
            return ips::ValueType::I32;
        }
        if (std::holds_alternative<f32>(folded)) {
            return ips::ValueType::F32;
        }
        return ips::ValueType::Color;
    }
    if (const auto* id = std::get_if<ips::Identifier>(&expr)) {
        if (id->name == "seed") {
            ins(Opcode::StreamState);
            return ips::ValueType::I32;
        }
        const u32 slot = current_->slots.at(id->name);
        ins(Opcode::LocalGet, 0, static_cast<u16>(slot));
        return current_->langTypes.at(id->name);
    }
    if (std::holds_alternative<ips::Member>(expr)) {
        return emitPartRead(expr);
    }
    if (const auto* call = std::get_if<ips::Call>(&expr)) {
        return emitCall(*call);
    }
    if (const auto* bin = std::get_if<ips::Binary>(&expr)) {
        return emitBinary(*bin);
    }
    if (const auto* un = std::get_if<ips::Unary>(&expr)) {
        return emitUnary(*un);
    }
    error(ips::locationOf(expr), "unsupported expression");
}

// A point value used by output writes and part placement: a vector literal
// {x, y} or a part read of a point.
void Emitter::emitPointValue(const ips::Expr& expr) {
    if (const auto* vector = std::get_if<ips::VectorLit>(&expr)) {
        for (const auto& element : vector->elements) {
            emitExpr(*element);
            if (plan_.typeEngine().typeOf(*element) == ips::ValueType::I32) {
                ins(Opcode::I32ToF32);
            }
        }
        return;
    }
    emitExpr(expr); // part.get.point
}

Emitter::PartRead Emitter::partReadInfo(const ips::Expr& expr) const {
    PartRead info;
    std::vector<std::string> reversed;
    const ips::Expr* current = &expr;
    while (const auto* member = std::get_if<ips::Member>(current)) {
        reversed.push_back(member->name);
        current = member->object.get();
    }
    const auto* base = std::get_if<ips::Identifier>(current);
    if (base == nullptr) {
        throw ips::SourceError{ips::locationOf(expr), "unsupported member access"};
    }
    info.alias = base->name;
    const auto part = plan_.partIndex.find(base->name);
    if (part == plan_.partIndex.end()) {
        throw ips::SourceError{ips::locationOf(expr),
                               "'" + base->name + "' is not an imported part"};
    }
    if (reversed.size() != 2 || reversed[1] != "outputs") {
        throw ips::SourceError{ips::locationOf(expr),
                               "expected 'part.outputs.<name>'"};
    }
    const ips::Program* program = plan_.parts[part->second];
    for (const auto& output : program->asset.outputs) {
        if (output.name == reversed[0]) {
            info.output = &output;
            info.kind = output.kind;
            return info;
        }
    }
    throw ips::SourceError{ips::locationOf(expr),
                           "part '" + base->name + "' has no output '" +
                               reversed[0] + "'"};
}

ips::ValueType Emitter::emitPartRead(const ips::Expr& expr) {
    const PartRead read = partReadInfo(expr);
    const u32 part = plan_.partIndex.at(read.alias);
    u32 slot = static_cast<u32>(plan_.rootSlots.size());
    for (u32 i = 0; i < part; ++i) {
        slot += static_cast<u32>(plan_.parts[i]->asset.outputs.size());
    }
    for (const auto& output : plan_.parts[part]->asset.outputs) {
        if (output.name == read.output->name) {
            break;
        }
        ++slot;
    }
    switch (read.kind) {
    case ips::OutputKind::Point:
        ins(Opcode::PartGetPoint, 0, static_cast<u16>(slot));
        return ips::ValueType::Point;
    case ips::OutputKind::Rect:
        ins(Opcode::PartGetRect, 0, static_cast<u16>(slot));
        return ips::ValueType::Rect;
    case ips::OutputKind::Circle:
        ins(Opcode::PartGetCircle, 0, static_cast<u16>(slot));
        return ips::ValueType::Circle;
    }
    error(ips::locationOf(expr), "unsupported part read");
}

ips::ValueType Emitter::emitBinary(const ips::Binary& bin) {
    const ips::ValueType leftType = plan_.typeEngine().typeOf(*bin.left);
    const ips::ValueType rightType = plan_.typeEngine().typeOf(*bin.right);
    const bool f32op = leftType == ips::ValueType::F32 ||
                       rightType == ips::ValueType::F32;

    switch (bin.op) {
    case ips::Binary::Op::Add:
    case ips::Binary::Op::Sub:
    case ips::Binary::Op::Mul:
    case ips::Binary::Op::Div: {
        emitExpr(*bin.left);
        if (f32op && leftType == ips::ValueType::I32) {
            ins(Opcode::I32ToF32);
        }
        emitExpr(*bin.right);
        if (f32op && rightType == ips::ValueType::I32) {
            ins(Opcode::I32ToF32);
        }
        if (f32op) {
            switch (bin.op) {
            case ips::Binary::Op::Add: ins(Opcode::F32Add); break;
            case ips::Binary::Op::Sub: ins(Opcode::F32Sub); break;
            case ips::Binary::Op::Mul: ins(Opcode::F32Mul); break;
            case ips::Binary::Op::Div: ins(Opcode::F32Div); break;
            default: break;
            }
            return ips::ValueType::F32;
        }
        switch (bin.op) {
        case ips::Binary::Op::Add: ins(Opcode::I32Add); break;
        case ips::Binary::Op::Sub: ins(Opcode::I32Sub); break;
        case ips::Binary::Op::Mul: ins(Opcode::I32Mul); break;
        case ips::Binary::Op::Div: ins(Opcode::I32DivS); break;
        default: break;
        }
        return ips::ValueType::I32;
    }
    case ips::Binary::Op::Mod: {
        // i32-only: the VM has no f32 remainder (the checker rejects it).
        emitExpr(*bin.left);
        emitExpr(*bin.right);
        ins(Opcode::I32RemS);
        return ips::ValueType::I32;
    }
    case ips::Binary::Op::Eq:
    case ips::Binary::Op::Ne:
    case ips::Binary::Op::Lt:
    case ips::Binary::Op::Le:
    case ips::Binary::Op::Gt:
    case ips::Binary::Op::Ge: {
        emitExpr(*bin.left);
        if (f32op && leftType == ips::ValueType::I32) {
            ins(Opcode::I32ToF32);
        }
        emitExpr(*bin.right);
        if (f32op && rightType == ips::ValueType::I32) {
            ins(Opcode::I32ToF32);
        }
        if (f32op) {
            switch (bin.op) {
            case ips::Binary::Op::Eq: ins(Opcode::F32Eq); break;
            case ips::Binary::Op::Ne: ins(Opcode::F32Ne); break;
            case ips::Binary::Op::Lt: ins(Opcode::F32Lt); break;
            case ips::Binary::Op::Le: ins(Opcode::F32Le); break;
            case ips::Binary::Op::Gt: ins(Opcode::F32Gt); break;
            case ips::Binary::Op::Ge: ins(Opcode::F32Ge); break;
            default: break;
            }
            return ips::ValueType::I32;
        }
        switch (bin.op) {
        case ips::Binary::Op::Eq: ins(Opcode::I32Eq); break;
        case ips::Binary::Op::Ne: ins(Opcode::I32Ne); break;
        case ips::Binary::Op::Lt: ins(Opcode::I32LtS); break;
        case ips::Binary::Op::Le: ins(Opcode::I32LeS); break;
        case ips::Binary::Op::Gt: ins(Opcode::I32GtS); break;
        case ips::Binary::Op::Ge: ins(Opcode::I32GeS); break;
        default: break;
        }
        return ips::ValueType::I32;
    }
    case ips::Binary::Op::Xor:
    case ips::Binary::Op::BitAnd:
    case ips::Binary::Op::BitOr: {
        emitExpr(*bin.left);
        emitExpr(*bin.right);
        switch (bin.op) {
        case ips::Binary::Op::Xor: ins(Opcode::I32Xor); break;
        case ips::Binary::Op::BitAnd: ins(Opcode::I32And); break;
        case ips::Binary::Op::BitOr: ins(Opcode::I32Or); break;
        default: break;
        }
        return ips::ValueType::I32;
    }
    case ips::Binary::Op::And:
    case ips::Binary::Op::Or: {
        // Short-circuit with 0/1 normalisation; the scratch local is reused
        // for every && / || in the function.
        emitExpr(*bin.left);
        ins(Opcode::If);
        if (bin.op == ips::Binary::Op::And) {
            emitExpr(*bin.right);
            ins(Opcode::ConstI32, 0, static_cast<u16>(plan_.pools.addInt(0)));
            ins(Opcode::I32Ne);
        } else {
            ins(Opcode::ConstI32, 0, static_cast<u16>(plan_.pools.addInt(1)));
        }
        ins(Opcode::LocalSet, 0, static_cast<u16>(scratch()));
        ins(Opcode::Else);
        if (bin.op == ips::Binary::Op::And) {
            ins(Opcode::ConstI32, 0, static_cast<u16>(plan_.pools.addInt(0)));
        } else {
            emitExpr(*bin.right);
            ins(Opcode::ConstI32, 0, static_cast<u16>(plan_.pools.addInt(0)));
            ins(Opcode::I32Ne);
        }
        ins(Opcode::LocalSet, 0, static_cast<u16>(scratch()));
        ins(Opcode::End);
        ins(Opcode::LocalGet, 0, static_cast<u16>(scratch()));
        return ips::ValueType::I32;
    }
    }
    error(bin.location, "unsupported binary operator");
}

ips::ValueType Emitter::emitUnary(const ips::Unary& un) {
    const ips::ValueType operand = plan_.typeEngine().typeOf(*un.operand);
    emitExpr(*un.operand);
    switch (un.op) {
    case ips::Unary::Op::Neg:
        ins(operand == ips::ValueType::F32 ? Opcode::F32Neg : Opcode::I32Neg);
        return operand;
    case ips::Unary::Op::Not:
        ins(Opcode::ConstI32, 0, static_cast<u16>(plan_.pools.addInt(0)));
        ins(Opcode::I32Eq);
        return ips::ValueType::I32;
    case ips::Unary::Op::BitNot:
        ins(Opcode::I32Not);
        return ips::ValueType::I32;
    }
    error(un.location, "unsupported unary operator");
}

} // namespace iprop::compiler
