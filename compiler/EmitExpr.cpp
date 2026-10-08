#include "Emit.hpp"
#include "PaletteLowerer.hpp"

#include <utility>

#include "ips/Fold.hpp"

namespace iprop::compiler {

// ---- expressions -------------------------------------------------------------

ips::ValueType Emitter::emitExpr(const ips::Expr& expr) {
    currentLine_ = static_cast<u32>(ips::locationOf(expr).line);
    if (const auto& constant = current_->expression(expr).constant; constant) {
        const auto& folded = *constant;
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
    if (const auto* vector = std::get_if<ips::VectorLit>(&expr)) {
        // A {x, y} literal in a value context becomes the vec2; the
        // 3/4-element compounds only flow into output writes.
        if (vector->elements.size() != 2) {
            error(ips::locationOf(expr),
                  "rect/circle literals only flow into output writes");
        }
        for (const auto& element : vector->elements) {
            emitExpr(*element);
            if (current_->typeOf(*element) == ips::ValueType::I32) {
                ins(Opcode::I32ToF32);
            }
        }
        ins(Opcode::Vec2Make);
        return ips::ValueType::Vec2;
    }
    if (std::holds_alternative<ips::Member>(expr)) {
        const auto& member = std::get<ips::Member>(expr);
        // Components yield f32 and color channels i32; bare part outputs keep
        // their compound type. This also distinguishes part.outputs.x from
        // part.outputs.top.x. The object is evaluated exactly once.
        const auto type = current_->typeOf(expr);
        if (type == ips::ValueType::F32) {
            emitExpr(*member.object);
            ins(member.name == "x" ? Opcode::Vec2X : Opcode::Vec2Y);
            return ips::ValueType::F32;
        }
        if (type == ips::ValueType::I32) {
            constexpr Opcode channels[] = {Opcode::ColorR, Opcode::ColorG, Opcode::ColorB, Opcode::ColorA};
            emitExpr(*member.object);
            ins(channels[*ips::colorChannel(member.name)]);
            return ips::ValueType::I32;
        }
        return emitPartRead(expr);
    }
    if (const auto* call = std::get_if<ips::Call>(&expr)) {
        return emitCall(*call);
    }
    if (const auto* index = std::get_if<ips::Index>(&expr)) {
        return PaletteLowerer(*this, *current_->expression(expr).palette).index(*index->index);
    }
    if (const auto* bin = std::get_if<ips::Binary>(&expr)) {
        return emitBinary(*bin);
    }
    if (const auto* un = std::get_if<ips::Unary>(&expr)) {
        return emitUnary(*un);
    }
    if (const auto* range = std::get_if<ips::RandomRange>(&expr)) {
        // Lower, then upper, then the distribution, each once; the draw
        // follows all of their effects.
        const auto type = current_->typeOf(expr);
        const bool integer = type == ips::ValueType::I32;
        const bool ends = type == ips::ValueType::Color || type == ips::ValueType::Vec2;
        for (const auto* bound : {range->lower.get(), range->upper.get()}) {
            emitExpr(*bound);
            if (!integer && !ends && current_->typeOf(*bound) == ips::ValueType::I32) ins(Opcode::I32ToF32);
        }
        if (ends) {
            // Colors and vec2 share one position t = (0.0 to 1.0 [with d]).
            emitConst(0.0f);
            emitConst(1.0f);
        }
        if (range->distribution) {
            emitExpr(*range->distribution);
            ins(integer ? Opcode::RngIntWith : Opcode::RngFloatWith);
        } else {
            ins(integer ? Opcode::RngInt : Opcode::RngFloat);
        }
        if (ends) ins(type == ips::ValueType::Color ? Opcode::ColorMixRgba : Opcode::Vec2Mix);
        return type;
    }
    error(ips::locationOf(expr), "unsupported expression");
}

// A point value used by output writes and part placement: a vector literal
// {x, y} (the pair, as in v0.2), a bare part read of a point (the pair —
// the v0.2 byte-identical lowering), or any other vec2 value (unpacked via
// the swizzles).
void Emitter::emitPointValue(const ips::Expr& expr) {
    if (const auto* vector = std::get_if<ips::VectorLit>(&expr)) {
        for (const auto& element : vector->elements) {
            emitExpr(*element);
            if (current_->typeOf(*element) == ips::ValueType::I32) {
                ins(Opcode::I32ToF32);
            }
        }
        return;
    }
    if (std::holds_alternative<ips::Member>(expr)) {
        // part.outputs.<name> — the bare pair, byte-identical to v0.2.
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
        ins(Opcode::PartGetPoint, 0, static_cast<u16>(slot));
        return;
    }
    // A general vec2 value: unpack to (x, y) with y on top.
    emitExpr(expr);
    const auto y = temporary(ips::ValueType::F32);
    ins(Opcode::Dup);
    ins(Opcode::Vec2Y);
    ins(Opcode::LocalSet, 0, y.index());
    ins(Opcode::Vec2X);
    ins(Opcode::LocalGet, 0, y.index());
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
        ins(Opcode::Vec2Make);
        return ips::ValueType::Vec2;
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
    const ips::ValueType leftType = current_->typeOf(*bin.left);
    const ips::ValueType rightType = current_->typeOf(*bin.right);
    const bool f32op = leftType == ips::ValueType::F32 ||
                       rightType == ips::ValueType::F32;
    const bool vec2op = leftType == ips::ValueType::Vec2 ||
                        rightType == ips::ValueType::Vec2;

    switch (bin.op) {
    case ips::Binary::Op::Add:
    case ips::Binary::Op::Sub:
    case ips::Binary::Op::Mul:
    case ips::Binary::Op::Div: {
        if (vec2op) {
            const bool rightIsVector = rightType == ips::ValueType::Vec2;
            if (bin.op == ips::Binary::Op::Mul && rightIsVector &&
                leftType != ips::ValueType::Vec2) {
                // Evaluate left first, but arrange the VM operands as (v, s).
                // A distinct local keeps s alive across nested products and
                // builtins that need their own temporaries while evaluating v.
                emitExpr(*bin.left);
                if (leftType == ips::ValueType::I32) {
                    ins(Opcode::I32ToF32);
                }
                const auto scalar = temporary(ips::ValueType::F32);
                ins(Opcode::LocalSet, 0, scalar.index());
                emitExpr(*bin.right);
                ins(Opcode::LocalGet, 0, scalar.index());
            } else {
                emitExpr(*bin.left);
                emitExpr(*bin.right);
                if (rightType == ips::ValueType::I32) {
                    ins(Opcode::I32ToF32);
                }
            }
            switch (bin.op) {
            case ips::Binary::Op::Add: ins(Opcode::Vec2Add); break;
            case ips::Binary::Op::Sub: ins(Opcode::Vec2Sub); break;
            case ips::Binary::Op::Mul: ins(Opcode::Vec2Mul); break;
            case ips::Binary::Op::Div: ins(Opcode::Vec2Div); break;
            default: break;
            }
            return ips::ValueType::Vec2;
        }
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
    case ips::Binary::Op::BitOr:
    case ips::Binary::Op::Shl:
    case ips::Binary::Op::Shr: {
        emitExpr(*bin.left);
        emitExpr(*bin.right);
        switch (bin.op) {
        case ips::Binary::Op::Xor: ins(Opcode::I32Xor); break;
        case ips::Binary::Op::BitAnd: ins(Opcode::I32And); break;
        case ips::Binary::Op::BitOr: ins(Opcode::I32Or); break;
        case ips::Binary::Op::Shl: ins(Opcode::I32Shl); break;
        case ips::Binary::Op::Shr: ins(Opcode::I32ShrU); break; // u32 semantics
        default: break;
        }
        return ips::ValueType::I32;
    }
    case ips::Binary::Op::And:
    case ips::Binary::Op::Or: {
        // Short-circuit with 0/1 normalisation; nested expressions retain
        // distinct result slots until the corresponding join.
        emitExpr(*bin.left);
        const auto result = temporary(ips::ValueType::I32);
        ins(Opcode::If);
        if (bin.op == ips::Binary::Op::And) {
            emitExpr(*bin.right);
            ins(Opcode::ConstI32, 0, static_cast<u16>(plan_.pools.addInt(0)));
            ins(Opcode::I32Ne);
        } else {
            ins(Opcode::ConstI32, 0, static_cast<u16>(plan_.pools.addInt(1)));
        }
        ins(Opcode::LocalSet, 0, result.index());
        ins(Opcode::Else);
        if (bin.op == ips::Binary::Op::And) {
            ins(Opcode::ConstI32, 0, static_cast<u16>(plan_.pools.addInt(0)));
        } else {
            emitExpr(*bin.right);
            ins(Opcode::ConstI32, 0, static_cast<u16>(plan_.pools.addInt(0)));
            ins(Opcode::I32Ne);
        }
        ins(Opcode::LocalSet, 0, result.index());
        ins(Opcode::End);
        ins(Opcode::LocalGet, 0, result.index());
        return ips::ValueType::I32;
    }
    }
    error(bin.location, "unsupported binary operator");
}

ips::ValueType Emitter::emitUnary(const ips::Unary& un) {
    const ips::ValueType operand = current_->typeOf(*un.operand);
    emitExpr(*un.operand);
    switch (un.op) {
    case ips::Unary::Op::Neg:
        if (operand == ips::ValueType::Vec2) {
            ins(Opcode::Vec2Neg);
        } else {
            ins(operand == ips::ValueType::F32 ? Opcode::F32Neg
                                               : Opcode::I32Neg);
        }
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
