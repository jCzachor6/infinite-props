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
    if (const auto* vector = std::get_if<ips::VectorLit>(&expr)) {
        // A {x, y} literal in a value context becomes the vec2; the
        // 3/4-element compounds only flow into output writes.
        if (vector->elements.size() != 2) {
            error(ips::locationOf(expr),
                  "rect/circle literals only flow into output writes");
        }
        for (const auto& element : vector->elements) {
            emitExpr(*element);
            if (plan_.typeEngine().typeOf(*element) == ips::ValueType::I32) {
                ins(Opcode::I32ToF32);
            }
        }
        ins(Opcode::Vec2Make);
        return ips::ValueType::Vec2;
    }
    if (std::holds_alternative<ips::Member>(expr)) {
        const auto& member = std::get<ips::Member>(expr);
        // Components yield f32; bare part outputs keep their compound type.
        // This also distinguishes part.outputs.x from part.outputs.top.x.
        if (plan_.typeEngine().typeOf(expr) == ips::ValueType::F32) {
            emitExpr(*member.object);
            ins(member.name == "x" ? Opcode::Vec2X : Opcode::Vec2Y);
            return ips::ValueType::F32;
        }
        return emitPartRead(expr);
    }
    if (const auto* call = std::get_if<ips::Call>(&expr)) {
        // Expression statements may not have been typed by the planner.
        // Validate arity/types before the emitter indexes builtin arguments.
        plan_.typeEngine().typeOf(expr);
        return emitCall(*call);
    }
    if (const auto* index = std::get_if<ips::Index>(&expr)) {
        return emitIndex(*index);
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
// {x, y} (the pair, as in v0.2), a bare part read of a point (the pair —
// the v0.2 byte-identical lowering), or any other vec2 value (unpacked via
// the swizzles).
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
    ins(Opcode::Dup);
    ins(Opcode::Vec2Y);
    ins(Opcode::LocalSet, 0, static_cast<u16>(f32Scratch()));
    ins(Opcode::Vec2X);
    ins(Opcode::LocalGet, 0, static_cast<u16>(f32Scratch()));
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
    const ips::ValueType leftType = plan_.typeEngine().typeOf(*bin.left);
    const ips::ValueType rightType = plan_.typeEngine().typeOf(*bin.right);
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
                // builtins that use the shared f32 scratch while evaluating v.
                emitExpr(*bin.left);
                if (leftType == ips::ValueType::I32) {
                    ins(Opcode::I32ToF32);
                }
                const u16 scalar = static_cast<u16>(allocateLocal(ValueType::F32));
                ins(Opcode::LocalSet, 0, scalar);
                emitExpr(*bin.right);
                ins(Opcode::LocalGet, 0, scalar);
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

// `palette[i]` — the hand-written picker semantics, desugared: evaluate
// the index once, default to the last element, override for every earlier
// element. A constant index folds to the element (an out-of-range constant
// yields the default, matching the runtime ladder). No VM change.
ips::ValueType Emitter::emitIndex(const ips::Index& index) {
    const auto* name = std::get_if<ips::Identifier>(index.object.get());
    if (name == nullptr) {
        error(locationOf(*index.object), "only palettes can be indexed");
    }
    const ips::PaletteDecl* palette = nullptr;
    for (const auto& candidate : current_->owner->asset.palettes) {
        if (candidate.name == name->name) {
            palette = &candidate;
            break;
        }
    }
    if (palette == nullptr) {
        error(name->location, "'" + name->name + "' is not a palette");
    }

    const auto elementType = [&](const ips::Expr& element) {
        const auto* literal = std::get_if<ips::Literal>(&element);
        if (literal == nullptr) {
            error(locationOf(element), "palette elements must be literals");
        }
        switch (literal->kind) {
        case ips::Literal::Kind::Int:
        case ips::Literal::Kind::True:
        case ips::Literal::Kind::False:
            return ips::ValueType::I32;
        case ips::Literal::Kind::Float:
            return ips::ValueType::F32;
        case ips::Literal::Kind::Color:
            return ips::ValueType::Color;
        case ips::Literal::Kind::String:
            break;
        }
        error(locationOf(element), "palette elements must be literals");
    };
    const auto emitElement = [&](const ips::Expr& element) {
        Value folded;
        if (!foldExpression(element, folded)) {
            error(locationOf(element), "palette elements must be literals");
        }
        emitConst(folded);
    };

    const ips::ValueType type = elementType(*palette->elements[0]);
    const std::size_t count = palette->elements.size();

    // Constant indices fold; the fold matches the ladder for the
    // out-of-range and negative cases (the default element).
    if (Value foldedIndex; foldExpression(*index.index, foldedIndex)) {
        if (const auto* k = std::get_if<i32>(&foldedIndex)) {
            const std::size_t chosen =
                (*k >= 0 && static_cast<std::size_t>(*k) + 1 < count)
                    ? static_cast<std::size_t>(*k)
                    : count - 1;
            emitElement(*palette->elements[chosen]);
            return type;
        }
    }

    // Runtime ladder.
    emitExpr(*index.index);
    const u16 indexSlot = static_cast<u16>(scratch());
    ins(Opcode::LocalSet, 0, indexSlot);
    const u16 resultSlot = static_cast<u16>(scratchOf(type));
    // For i32/bool the result reuses the index local. Select exactly once
    // before overwriting it, and leave the ladder after a match.
    ins(Opcode::Block);
    for (std::size_t k = 0; k + 1 < count; ++k) {
        ins(Opcode::ConstI32, 0,
            static_cast<u16>(plan_.pools.addInt(static_cast<u32>(k))));
        ins(Opcode::LocalGet, 0, indexSlot);
        ins(Opcode::I32Eq);
        ins(Opcode::If);
        emitElement(*palette->elements[k]);
        ins(Opcode::LocalSet, 0, resultSlot);
        ins(Opcode::Br, 0, 1);
        ins(Opcode::End);
    }
    emitElement(*palette->elements.back());
    ins(Opcode::LocalSet, 0, resultSlot);
    ins(Opcode::End);
    ins(Opcode::LocalGet, 0, resultSlot);
    return type;
}

} // namespace iprop::compiler
