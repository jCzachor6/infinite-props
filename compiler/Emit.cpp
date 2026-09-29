#include "Emit.hpp"

#include <utility>

#include "Fold.hpp"
#include "Literals.hpp"

namespace iprop::compiler {

Emitter::Emitter(Plan& plan) : plan_(plan) {}

[[noreturn]] void Emitter::error(const ips::SourceLocation& at,
                                 std::string message) {
    throw ips::SourceError{at, std::move(message)};
}

void Emitter::ins(Opcode opcode, u8 aux, u16 operand) {
    code_.push_back(Instruction{opcode, aux, operand});
    lines_.push_back(currentLine_);
}

void Emitter::emitConst(const Value& value) {
    if (const auto* i = std::get_if<i32>(&value)) {
        ins(Opcode::ConstI32, 0,
            static_cast<u16>(plan_.pools.addInt(static_cast<u32>(*i))));
    } else if (const auto* f = std::get_if<f32>(&value)) {
        u32 bits = 0;
        std::memcpy(&bits, &*f, sizeof(bits));
        ins(Opcode::ConstF32, 0, static_cast<u16>(plan_.pools.addFloat(bits)));
    } else if (const auto* c = std::get_if<Color>(&value)) {
        ins(Opcode::ConstColor, 0, static_cast<u16>(plan_.pools.addColor(*c)));
    }
}

// Extra locals reused within a function: one i32 (short-circuit, round) and
// one f32 (round). Appended to the function's locals; the assembler writes
// the table after all emission.
u32 Emitter::scratch() {
    scratchUsed_ = true;
    return static_cast<u32>(current_->params.size() + current_->locals.size());
}

u32 Emitter::f32Scratch() {
    f32ScratchUsed_ = true;
    return static_cast<u32>(current_->params.size() + current_->locals.size() +
                            (scratchUsed_ ? 1 : 0));
}

bool Emitter::isParam(const std::string& name) const {
    for (const auto& param : current_->ast->params) {
        if (param.name == name) {
            return true;
        }
    }
    return false;
}

void Emitter::emitFunction(PlannedFunction& function) {
    current_ = &function;
    code_.clear();
    lines_.clear();
    currentLine_ = 0;
    scratchUsed_ = false;
    ips::Typer::Scope savedScope = plan_.typeEngine().saveScope();
    plan_.typeEngine().restoreScope(
        ips::Typer::Scope{function.owner, function.drawLike, function.langTypes});
    for (const auto& stmt : function.ast->body) {
        emitStmt(stmt);
    }
    plan_.typeEngine().restoreScope(std::move(savedScope));
    function.code = std::move(code_);
    function.sourceLines = std::move(lines_);
    if (scratchUsed_) {
        function.locals.push_back(ValueType::I32);
    }
    if (f32ScratchUsed_) {
        function.locals.push_back(ValueType::F32);
    }
    current_ = nullptr;
}

namespace {
// The source line of a statement node.
u32 stmtLine(const ips::Stmt& stmt) {
    return static_cast<u32>(std::visit(
        [](const auto& node) { return node.location.line; }, stmt));
}
} // namespace

void Emitter::emitStmt(const ips::Stmt& stmt) {
    currentLine_ = stmtLine(stmt);
    if (const auto* decl = std::get_if<ips::VarDecl>(&stmt)) {
        const ips::OutputDecl* output = nullptr;
        for (const auto& candidate : current_->owner->asset.outputs) {
            if (candidate.name == decl->name) {
                output = &candidate;
                break;
            }
        }
        if (output != nullptr) {
            emitOutputWrite(current_->owner, *output, *decl->value);
            return;
        }
        if (isParam(decl->name)) {
            return; // unreachable: the checker rejects parameter writes
        }
        emitExpr(*decl->value);
        ins(Opcode::LocalSet, 0, static_cast<u16>(current_->slots.at(decl->name)));
    } else if (const auto* exprStmt = std::get_if<ips::ExprStmt>(&stmt)) {
        const ips::ValueType type = emitExpr(*exprStmt->expr);
        if (type != ips::ValueType::Void) {
            ins(Opcode::Drop);
        }
    } else if (const auto* ifStmt = std::get_if<ips::IfStmt>(&stmt)) {
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
    } else if (const auto* whileStmt = std::get_if<ips::WhileStmt>(&stmt)) {
        ins(Opcode::Block);
        ins(Opcode::Loop);
        emitExpr(*whileStmt->condition);
        ins(Opcode::ConstI32, 0, static_cast<u16>(plan_.pools.addInt(0)));
        ins(Opcode::I32Eq);
        ins(Opcode::BrIf, 0, 1); // exit the enclosing block
        for (const auto& s : whileStmt->body) {
            emitStmt(s);
        }
        ins(Opcode::Br, 0, 0); // back to the loop start
        ins(Opcode::End);
        ins(Opcode::End);
    } else if (const auto* ret = std::get_if<ips::ReturnStmt>(&stmt)) {
        if (ret->value) {
            emitExpr(*ret->value);
        }
        ins(Opcode::Return);
    }
}

// The global output slot for a root output or a part output (spec §4.3:
// root slots first, then part slots in part order).
u32 Emitter::slotForOutput(const ips::Program* owner,
                           const ips::OutputDecl& output) const {
    if (owner == plan_.entry) {
        return plan_.rootSlotIndex.at(output.name);
    }
    u32 part = 0;
    while (plan_.parts[part] != owner) {
        ++part;
    }
    u32 slot = static_cast<u32>(plan_.rootSlots.size());
    for (u32 i = 0; i < part; ++i) {
        slot += static_cast<u32>(plan_.parts[i]->asset.outputs.size());
    }
    for (const auto& candidate : owner->asset.outputs) {
        if (candidate.name == output.name) {
            return slot;
        }
        ++slot;
    }
    return slot;
}

void Emitter::emitOutputWrite(const ips::Program* owner,
                              const ips::OutputDecl& output,
                              const ips::Expr& value) {
    const u32 slot = slotForOutput(owner, output);
    if (const auto* vector = std::get_if<ips::VectorLit>(&value)) {
        for (const auto& element : vector->elements) {
            emitExpr(*element);
            if (plan_.typeEngine().typeOf(*element) == ips::ValueType::I32) {
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

} // namespace iprop::compiler
