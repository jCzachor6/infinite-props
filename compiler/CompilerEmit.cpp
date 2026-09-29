#include "CompilerDetail.hpp"

#include <utility>

namespace iprop {
namespace detail {

// ---- instruction emission ---------------------------------------------------

void Compiler::ins(Opcode opcode, u8 aux, u16 operand) {
    code_.push_back(static_cast<u8>(opcode));
    code_.push_back(aux);
    code_.push_back(static_cast<u8>(operand));
    code_.push_back(static_cast<u8>(operand >> 8));
}

void Compiler::emitConst(const Value& value) {
    if (const auto* i = std::get_if<i32>(&value)) {
        ins(Opcode::ConstI32, 0,
            static_cast<u16>(pools_.addInt(static_cast<u32>(*i))));
    } else if (const auto* f = std::get_if<f32>(&value)) {
        u32 bits = 0;
        std::memcpy(&bits, &*f, sizeof(bits));
        ins(Opcode::ConstF32, 0, static_cast<u16>(pools_.addFloat(bits)));
    } else if (const auto* c = std::get_if<Color>(&value)) {
        ins(Opcode::ConstColor, 0, static_cast<u16>(pools_.addColor(*c)));
    }
}

// One extra i32 local reused by every && / || in the function. Appended to
// the function's locals; the table is written after all emission.
u32 Compiler::scratch() {
    scratchUsed_ = true;
    return static_cast<u32>(current_->params.size() + current_->locals.size());
}

void Compiler::emitAll() {
    for (auto& info : functions_) {
        current_ = &info;
        code_.clear();
        scratchUsed_ = false;
        ips::Typer::Scope savedScope = typer_.saveScope();
        typer_.restoreScope(
            ips::Typer::Scope{info.owner, info.drawLike, info.langTypes});
        for (const auto& stmt : info.ast->body) {
            emitStmt(stmt);
        }
        typer_.restoreScope(std::move(savedScope));
        info.code = std::move(code_);
        if (scratchUsed_) {
            info.locals.push_back(ValueType::I32);
        }
        current_ = nullptr;
    }
}

bool Compiler::isParam(const std::string& name) const {
    const auto& params = current_->ast->params;
    return std::find(params.begin(), params.end(), name) != params.end();
}

void Compiler::emitStmt(const Stmt& stmt) {
    if (const auto* decl = std::get_if<VarDecl>(&stmt)) {
        if (const OutputDecl* output = ownerOutput(current_->owner, decl->name)) {
            emitOutputWrite(current_->owner, *output, *decl->value);
            return;
        }
        if (ownerInput(current_->owner, decl->name) != nullptr ||
            isParam(decl->name)) {
            return; // unreachable: the checker rejects these
        }
        emitExpr(*decl->value);
        ins(Opcode::LocalSet, 0, static_cast<u16>(current_->slots.at(decl->name)));
    } else if (const auto* exprStmt = std::get_if<ExprStmt>(&stmt)) {
        const ips::ValueType type = emitExpr(*exprStmt->expr);
        if (type != ips::ValueType::Void) {
            ins(Opcode::Drop);
        }
    } else if (const auto* ifStmt = std::get_if<IfStmt>(&stmt)) {
        emitExpr(*ifStmt->condition);
        ins(Opcode::If);
        for (const auto& s : ifStmt->thenBranch) {
            emitStmt(s);
        }
        if (!ifStmt->elseBranch.empty()) {
            ins(Opcode::Else);
            for (const auto& s : ifStmt->elseBranch) {
                emitStmt(s);
            }
        }
        ins(Opcode::End);
    } else if (const auto* whileStmt = std::get_if<WhileStmt>(&stmt)) {
        ins(Opcode::Block);
        ins(Opcode::Loop);
        emitExpr(*whileStmt->condition);
        ins(Opcode::ConstI32, 0, static_cast<u16>(pools_.addInt(0)));
        ins(Opcode::I32Eq);
        ins(Opcode::BrIf, 0, 1); // exit the enclosing block
        for (const auto& s : whileStmt->body) {
            emitStmt(s);
        }
        ins(Opcode::Br, 0, 0); // back to the loop start
        ins(Opcode::End);
        ins(Opcode::End);
    } else if (const auto* ret = std::get_if<ReturnStmt>(&stmt)) {
        if (ret->value) {
            emitExpr(*ret->value);
        }
        ins(Opcode::Return);
    }
}

// The global output slot for a root output or a part output (spec §4.3:
// root slots first, then part slots in part order).
u32 Compiler::slotForOutput(const Program* owner, const OutputDecl& output) const {
    if (owner == &entry_) {
        return rootSlotIndex_.at(output.name);
    }
    u32 part = 0;
    while (orderedParts_[part] != owner) {
        ++part;
    }
    u32 slot = static_cast<u32>(rootSlots_.size());
    for (u32 i = 0; i < part; ++i) {
        slot += static_cast<u32>(orderedParts_[i]->asset.outputs.size());
    }
    for (const auto& candidate : owner->asset.outputs) {
        if (candidate.name == output.name) {
            return slot;
        }
        ++slot;
    }
    return slot;
}

void Compiler::emitOutputWrite(const Program* owner, const OutputDecl& output,
                               const Expr& value) {
    const u32 slot = slotForOutput(owner, output);
    if (const auto* vector = std::get_if<VectorLit>(&value)) {
        for (const auto& element : vector->elements) {
            emitExpr(*element);
            if (typer_.typeOf(*element) == ips::ValueType::I32) {
                ins(Opcode::I32ToF32);
            }
        }
    } else {
        emitExpr(value); // part.get.* pushes the right shape
    }
    switch (output.kind) {
    case ips::OutputKind::Point:
        ins(output.isList ? Opcode::OutListPoint : Opcode::OutPoint, 0,
            static_cast<u16>(slot));
        break;
    case ips::OutputKind::Rect:
        ins(output.isList ? Opcode::OutListRect : Opcode::OutRect, 0,
            static_cast<u16>(slot));
        break;
    case ips::OutputKind::Circle:
        ins(output.isList ? Opcode::OutListCircle : Opcode::OutCircle, 0,
            static_cast<u16>(slot));
        break;
    }
}

// ---- expressions -------------------------------------------------------------

ips::ValueType Compiler::emitExpr(const Expr& expr) {
    if (Value folded; tryFold(expr, folded)) {
        emitConst(folded);
        if (std::holds_alternative<i32>(folded)) {
            return ips::ValueType::I32;
        }
        if (std::holds_alternative<f32>(folded)) {
            return ips::ValueType::F32;
        }
        return ips::ValueType::Color;
    }
    if (const auto* id = std::get_if<Identifier>(&expr)) {
        if (id->name == "seed") {
            ins(Opcode::StreamState);
            return ips::ValueType::I32;
        }
        const u32 slot = current_->slots.at(id->name);
        ins(Opcode::LocalGet, 0, static_cast<u16>(slot));
        return current_->langTypes.at(id->name);
    }
    if (std::holds_alternative<Member>(expr)) {
        return emitPartRead(expr);
    }
    if (const auto* call = std::get_if<Call>(&expr)) {
        return emitCall(*call);
    }
    if (const auto* bin = std::get_if<Binary>(&expr)) {
        return emitBinary(*bin);
    }
    if (const auto* un = std::get_if<Unary>(&expr)) {
        return emitUnary(*un);
    }
    error(locationOf(expr), "unsupported expression");
}

// A point value used by output writes and part placement: a vector literal
// {x, y} or a part read of a point.
void Compiler::emitPointValue(const Expr& expr) {
    if (const auto* vector = std::get_if<VectorLit>(&expr)) {
        for (const auto& element : vector->elements) {
            emitExpr(*element);
            if (typer_.typeOf(*element) == ips::ValueType::I32) {
                ins(Opcode::I32ToF32);
            }
        }
        return;
    }
    emitExpr(expr); // part.get.point
}

Compiler::PartRead Compiler::partReadInfo(const Expr& expr) const {
    PartRead info;
    std::vector<std::string> reversed;
    const Expr* current = &expr;
    while (const auto* member = std::get_if<Member>(current)) {
        reversed.push_back(member->name);
        current = member->object.get();
    }
    const auto* base = std::get_if<Identifier>(current);
    if (base == nullptr) {
        throw SourceError{locationOf(expr), "unsupported member access"};
    }
    info.alias = base->name;
    const auto part = parts_.find(base->name);
    if (part == parts_.end()) {
        throw SourceError{locationOf(expr),
                          "'" + base->name + "' is not an imported part"};
    }
    if (reversed.size() != 2 || reversed[1] != "outputs") {
        throw SourceError{locationOf(expr), "expected 'part.outputs.<name>'"};
    }
    for (const auto& output : part->second->asset.outputs) {
        if (output.name == reversed[0]) {
            info.output = &output;
            info.kind = output.kind;
            return info;
        }
    }
    throw SourceError{locationOf(expr), "part '" + base->name +
                                            "' has no output '" + reversed[0] + "'"};
}

ips::ValueType Compiler::emitPartRead(const Expr& expr) {
    const PartRead read = partReadInfo(expr);
    const u32 part = partIndex_.at(read.alias);
    u32 slot = static_cast<u32>(rootSlots_.size());
    for (u32 i = 0; i < part; ++i) {
        slot += static_cast<u32>(orderedParts_[i]->asset.outputs.size());
    }
    for (const auto& output : orderedParts_[part]->asset.outputs) {
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
    error(locationOf(expr), "unsupported part read");
}

ips::ValueType Compiler::emitCall(const Call& call) {
    if (const auto* name = std::get_if<Identifier>(call.callee.get())) {
        if (ips::builtinSignatures().count(name->name) != 0) {
            return emitBuiltin(name->name, call);
        }
        if (functionIndex_.count(name->name) == 0) {
            prepareFunction(name->name);
        }
        const u32 index = functionIndex_.at(name->name);
        for (const auto& arg : call.args) {
            emitExpr(*arg.value);
        }
        ins(Opcode::Call, 0, static_cast<u16>(index));
        return functions_[index].langResult.value_or(ips::ValueType::Void);
    }
    if (std::holds_alternative<Member>(*call.callee)) {
        emitPartCall(call);
        return ips::ValueType::Void;
    }
    error(call.location, "cannot call this expression");
}

// Emits one argument and widens i32 to f32 when the builtin expects f32.
void Compiler::emitArg(const Arg& arg, ips::ValueType expected) {
    emitExpr(*arg.value);
    if (expected == ips::ValueType::F32 &&
        typer_.typeOf(*arg.value) == ips::ValueType::I32) {
        ins(Opcode::I32ToF32);
    }
}

ips::ValueType Compiler::emitBuiltin(const std::string& name, const Call& call) {
    using VT = ips::ValueType;
    const auto emitFixed = [&](std::initializer_list<VT> params,
                               Opcode opcode) -> VT {
        for (std::size_t i = 0; i < call.args.size(); ++i) {
            emitArg(call.args[i], params.begin()[i]);
        }
        ins(opcode);
        return ips::builtinSignatures().at(name).result;
    };

    if (name == "unit") {
        ins(Opcode::RngUnit);
        return VT::F32;
    }
    if (name == "range") {
        emitArg(call.args[0], VT::F32);
        emitArg(call.args[1], VT::F32);
        ins(Opcode::RngRange);
        return VT::F32;
    }
    if (name == "index") return emitFixed({VT::I32}, Opcode::RngIndex);
    if (name == "hash") return emitFixed({VT::I32}, Opcode::Hash);
    if (name == "noise") {
        return emitFixed({VT::F32, VT::F32, VT::I32}, Opcode::Noise);
    }
    if (name == "fault") {
        return emitFixed({VT::F32, VT::F32, VT::I32}, Opcode::Fault);
    }
    if (name == "smooth") return emitFixed({VT::F32}, Opcode::Smooth);
    if (name == "lerp") {
        return emitFixed({VT::F32, VT::F32, VT::F32}, Opcode::Lerp);
    }
    if (name == "mix") {
        return emitFixed({VT::Color, VT::Color, VT::F32}, Opcode::ColorMix);
    }
    if (name == "shade") {
        return emitFixed({VT::Color, VT::I32}, Opcode::ColorShade);
    }
    if (name == "pixel") {
        return emitFixed({VT::I32, VT::I32, VT::Color}, Opcode::Pixel);
    }
    if (name == "rect") {
        return emitFixed({VT::F32, VT::F32, VT::F32, VT::F32, VT::Color},
                         Opcode::Rect);
    }
    if (name == "line") {
        return emitFixed({VT::F32, VT::F32, VT::F32, VT::F32, VT::I32,
                          VT::Color}, Opcode::Line);
    }
    if (name == "oval") {
        return emitFixed({VT::F32, VT::F32, VT::F32, VT::F32, VT::Color},
                         Opcode::Oval);
    }
    if (name == "weather") {
        return emitFixed({VT::I32, VT::I32}, Opcode::Weather);
    }
    if (name == "poly") {
        // Stack order (top first, spec §9.8): colour on top, then the
        // vertex pairs — so the pairs are pushed first, the colour last.
        for (std::size_t i = 1; i < call.args.size(); ++i) {
            emitArg(call.args[i], VT::F32);
        }
        emitArg(call.args[0], VT::Color);
        const u16 vertices = static_cast<u16>((call.args.size() - 1) / 2);
        ins(Opcode::Poly, 0, vertices);
        return VT::Void;
    }
    error(call.location, "undefined function '" + name + "'");
}

// `part.draw(seed, variant, archetype, at = {x, y}, uniforms...)`
void Compiler::emitPartCall(const Call& call) {
    std::vector<std::string> reversed;
    const Expr* current = call.callee.get();
    while (const auto* member = std::get_if<Member>(current)) {
        reversed.push_back(member->name);
        current = member->object.get();
    }
    const auto* base = std::get_if<Identifier>(current);
    const std::string alias = base->name;
    const Program* part = parts_.at(alias);
    const u32 partIndex = partIndex_.at(alias);

    // Uniforms in declaration order (bottom of the stack): override with the
    // named argument, else push the declared default.
    for (const auto& input : part->asset.inputs) {
        const Arg* overrideArg = nullptr;
        for (const auto& arg : call.args) {
            if (arg.name.has_value() && *arg.name == input.name) {
                overrideArg = &arg;
                break;
            }
        }
        if (overrideArg != nullptr) {
            emitExpr(*overrideArg->value);
            continue;
        }
        const auto* value = std::get_if<Literal>(input.defaultValue.get());
        Value folded;
        if (value == nullptr || !tryFold(*value, folded)) {
            error(input.location,
                  "part input '" + input.name + "' has no constant default");
        }
        emitConst(folded);
    }

    // seed, variant, archetype (positional, in order).
    for (const auto& arg : call.args) {
        if (!arg.name.has_value()) {
            emitExpr(*arg.value);
        }
    }

    // at = {x, y}
    for (const auto& arg : call.args) {
        if (arg.name.has_value() && *arg.name == "at") {
            emitPointValue(*arg.value);
            break;
        }
    }
    ins(Opcode::PartDraw, 0, static_cast<u16>(partIndex));
}

ips::ValueType Compiler::emitBinary(const Binary& bin) {
    const ips::ValueType leftType = typer_.typeOf(*bin.left);
    const ips::ValueType rightType = typer_.typeOf(*bin.right);
    const bool f32op = leftType == ips::ValueType::F32 ||
                       rightType == ips::ValueType::F32;

    switch (bin.op) {
    case Binary::Op::Add:
    case Binary::Op::Sub:
    case Binary::Op::Mul:
    case Binary::Op::Div: {
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
            case Binary::Op::Add: ins(Opcode::F32Add); break;
            case Binary::Op::Sub: ins(Opcode::F32Sub); break;
            case Binary::Op::Mul: ins(Opcode::F32Mul); break;
            case Binary::Op::Div: ins(Opcode::F32Div); break;
            default: break;
            }
            return ips::ValueType::F32;
        }
        switch (bin.op) {
        case Binary::Op::Add: ins(Opcode::I32Add); break;
        case Binary::Op::Sub: ins(Opcode::I32Sub); break;
        case Binary::Op::Mul: ins(Opcode::I32Mul); break;
        case Binary::Op::Div: ins(Opcode::I32DivS); break;
        default: break;
        }
        return ips::ValueType::I32;
    }
    case Binary::Op::Mod: {
        // i32-only: the VM has no f32 remainder (the checker rejects it).
        emitExpr(*bin.left);
        emitExpr(*bin.right);
        ins(Opcode::I32RemS);
        return ips::ValueType::I32;
    }
    case Binary::Op::Eq:
    case Binary::Op::Ne:
    case Binary::Op::Lt:
    case Binary::Op::Le:
    case Binary::Op::Gt:
    case Binary::Op::Ge: {
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
            case Binary::Op::Eq: ins(Opcode::F32Eq); break;
            case Binary::Op::Ne: ins(Opcode::F32Ne); break;
            case Binary::Op::Lt: ins(Opcode::F32Lt); break;
            case Binary::Op::Le: ins(Opcode::F32Le); break;
            case Binary::Op::Gt: ins(Opcode::F32Gt); break;
            case Binary::Op::Ge: ins(Opcode::F32Ge); break;
            default: break;
            }
            return ips::ValueType::I32;
        }
        switch (bin.op) {
        case Binary::Op::Eq: ins(Opcode::I32Eq); break;
        case Binary::Op::Ne: ins(Opcode::I32Ne); break;
        case Binary::Op::Lt: ins(Opcode::I32LtS); break;
        case Binary::Op::Le: ins(Opcode::I32LeS); break;
        case Binary::Op::Gt: ins(Opcode::I32GtS); break;
        case Binary::Op::Ge: ins(Opcode::I32GeS); break;
        default: break;
        }
        return ips::ValueType::I32;
    }
    case Binary::Op::Xor:
    case Binary::Op::BitAnd:
    case Binary::Op::BitOr: {
        emitExpr(*bin.left);
        emitExpr(*bin.right);
        switch (bin.op) {
        case Binary::Op::Xor: ins(Opcode::I32Xor); break;
        case Binary::Op::BitAnd: ins(Opcode::I32And); break;
        case Binary::Op::BitOr: ins(Opcode::I32Or); break;
        default: break;
        }
        return ips::ValueType::I32;
    }
    case Binary::Op::And:
    case Binary::Op::Or: {
        // Short-circuit with 0/1 normalisation; the scratch local is reused
        // for every && / || in the function.
        emitExpr(*bin.left);
        ins(Opcode::If);
        if (bin.op == Binary::Op::And) {
            emitExpr(*bin.right);
            ins(Opcode::ConstI32, 0, static_cast<u16>(pools_.addInt(0)));
            ins(Opcode::I32Ne);
        } else {
            ins(Opcode::ConstI32, 0, static_cast<u16>(pools_.addInt(1)));
        }
        ins(Opcode::LocalSet, 0, static_cast<u16>(scratch()));
        ins(Opcode::Else);
        if (bin.op == Binary::Op::And) {
            ins(Opcode::ConstI32, 0, static_cast<u16>(pools_.addInt(0)));
        } else {
            emitExpr(*bin.right);
            ins(Opcode::ConstI32, 0, static_cast<u16>(pools_.addInt(0)));
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

ips::ValueType Compiler::emitUnary(const Unary& un) {
    const ips::ValueType operand = typer_.typeOf(*un.operand);
    emitExpr(*un.operand);
    switch (un.op) {
    case Unary::Op::Neg:
        ins(operand == ips::ValueType::F32 ? Opcode::F32Neg : Opcode::I32Neg);
        return operand;
    case Unary::Op::Not:
        ins(Opcode::ConstI32, 0, static_cast<u16>(pools_.addInt(0)));
        ins(Opcode::I32Eq);
        return ips::ValueType::I32;
    case Unary::Op::BitNot:
        ins(Opcode::I32Not);
        return ips::ValueType::I32;
    }
    error(un.location, "unsupported unary operator");
}

} // namespace detail
} // namespace iprop
