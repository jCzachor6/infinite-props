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
    // Track the open control frames so that `break`/`continue` can compute
    // the branch depth to the enclosing while's labels. Every label-pushing
    // opcode has a matching End in the validated output, so the vector is
    // balanced and empty at every function boundary.
    if (opcode == Opcode::If || opcode == Opcode::Block) {
        labelKinds_.push_back(0);
    } else if (opcode == Opcode::Loop) {
        labelKinds_.push_back(1);
    } else if (opcode == Opcode::End) {
        labelKinds_.pop_back();
    }
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

// Allocate a typed local on first use. Its index stays fixed even when a
// different temporary type is needed later in this function.
u32 Emitter::allocateScratch(std::optional<u32>& slot, ValueType type) {
    if (!slot) {
        slot = static_cast<u32>(current_->params.size() + current_->locals.size());
        current_->locals.push_back(type);
    }
    return *slot;
}

u32 Emitter::scratch() {
    return allocateScratch(scratchSlot_, ValueType::I32);
}

u32 Emitter::f32Scratch() {
    return allocateScratch(f32ScratchSlot_, ValueType::F32);
}

u32 Emitter::colorScratch() {
    return allocateScratch(colorScratchSlot_, ValueType::Color);
}

// The extra local of the given scalar type.
u32 Emitter::scratchOf(ips::ValueType type) {
    switch (type) {
    case ips::ValueType::I32: return scratch();
    case ips::ValueType::F32: return f32Scratch();
    default: return colorScratch();
    }
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
    scratchSlot_.reset();
    f32ScratchSlot_.reset();
    colorScratchSlot_.reset();
    labelKinds_.clear();
    loopLabels_.clear();
    forCount_ = 0;
    ips::Typer::Scope savedScope = plan_.typeEngine().saveScope();
    const std::string qualified = function.name;
    const auto dot = qualified.find('.');
    plan_.typeEngine().restoreScope(ips::Typer::Scope{
        function.owner, function.drawLike,
        dot != std::string::npos ? qualified.substr(0, dot + 1) : "",
        function.langTypes});
    for (const auto& stmt : function.ast->body) {
        emitStmt(stmt);
    }
    plan_.typeEngine().restoreScope(std::move(savedScope));
    function.code = std::move(code_);
    function.sourceLines = std::move(lines_);
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
        const u32 blockIndex = static_cast<u32>(labelKinds_.size()) - 2;
        loopLabels_.push_back({blockIndex, blockIndex + 1});
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
        loopLabels_.pop_back();
    } else if (const auto* forStmt = std::get_if<ips::ForStmt>(&stmt)) {
        // for i in a..b { body } lowers to: counter = a; bound = b (both
        // evaluated once); then block/loop/block — the condition head, the
        // body (continue branches to the end of the inner block, i.e. the
        // increment), the increment, and the back edge.
        const u16 counter =
            static_cast<u16>(current_->slots.at(forStmt->variable));
        const std::string boundName = "$for" + std::to_string(forCount_++);
        const u16 bound = static_cast<u16>(current_->slots.at(boundName));
        emitExpr(*forStmt->from);
        ins(Opcode::LocalSet, 0, counter);
        emitExpr(*forStmt->to);
        ins(Opcode::LocalSet, 0, bound);
        ins(Opcode::Block); // the break label
        ins(Opcode::Loop);  // the condition head
        const u32 breakIndex = static_cast<u32>(labelKinds_.size()) - 2;
        ins(Opcode::LocalGet, 0, counter);
        ins(Opcode::LocalGet, 0, bound);
        ins(Opcode::I32LtS);
        ins(Opcode::ConstI32, 0, static_cast<u16>(plan_.pools.addInt(0)));
        ins(Opcode::I32Eq);
        ins(Opcode::BrIf, 0, 1); // exit the block when counter >= bound
        ins(Opcode::Block);      // the continue label (the increment frame)
        const u32 continueIndex = static_cast<u32>(labelKinds_.size()) - 1;
        loopLabels_.push_back({breakIndex, continueIndex});
        for (const auto& s : forStmt->body) {
            emitStmt(s);
        }
        loopLabels_.pop_back();
        ins(Opcode::End); // the increment frame
        ins(Opcode::LocalGet, 0, counter);
        ins(Opcode::ConstI32, 0, static_cast<u16>(plan_.pools.addInt(1)));
        ins(Opcode::I32Add);
        ins(Opcode::LocalSet, 0, counter);
        ins(Opcode::Br, 0, 0); // back to the condition
        ins(Opcode::End);
        ins(Opcode::End);
    } else if (const auto* brk = std::get_if<ips::BreakStmt>(&stmt)) {
        if (loopLabels_.empty()) {
            error(brk->location, "'break' outside a loop");
        }
        const auto [blockIndex, loopIndex] = loopLabels_.back();
        (void)loopIndex;
        ins(Opcode::Br, 0,
            static_cast<u16>(labelKinds_.size() - 1 - blockIndex));
    } else if (const auto* cont = std::get_if<ips::ContinueStmt>(&stmt)) {
        if (loopLabels_.empty()) {
            error(cont->location, "'continue' outside a loop");
        }
        const auto [blockIndex, loopIndex] = loopLabels_.back();
        (void)blockIndex;
        ins(Opcode::Br, 0,
            static_cast<u16>(labelKinds_.size() - 1 - loopIndex));
    } else if (const auto* ret = std::get_if<ips::ReturnStmt>(&stmt)) {
        if (ret->value) {
            emitExpr(*ret->value);
            // A declared f32 result widens an i32 return value.
            if (!current_->results.empty() &&
                current_->results[0] == ValueType::F32 &&
                plan_.typeEngine().typeOf(*ret->value) == ips::ValueType::I32) {
                ins(Opcode::I32ToF32);
            }
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
