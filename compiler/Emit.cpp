#include "Emit.hpp"

#include <utility>

#include "ips/Fold.hpp"
#include "ips/Literals.hpp"

namespace iprop::compiler {

Emitter::Emitter(Plan& plan, bool unrollLoops) : plan_(plan), unrollLoops_(unrollLoops) {}

[[noreturn]] void Emitter::error(const ips::SourceLocation& at,
                                 std::string message) {
    throw ips::SourceError{at, std::move(message)};
}

void Emitter::ins(Opcode opcode, u8 aux, u16 operand) {
    // Track the open control frames so that `break`/`continue` can compute
    // the branch depth to the enclosing while's labels. Every label-pushing
    // opcode has a matching End in the validated output, so the vector is
    // balanced and empty at every function boundary.
    if (opcode == Opcode::If || opcode == Opcode::Block || opcode == Opcode::LayerBegin) {
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
    } else if (const auto* vector = std::get_if<Vec2>(&value)) {
        emitConst(vector->x); emitConst(vector->y);
        ins(Opcode::Vec2Make);
    } else if (const auto* list = std::get_if<ListValue>(&value)) {
        ins(Opcode::ConstList, 0, plan_.pools.addList(*list));
    } else if (const auto* sampler = std::get_if<SamplerValue>(&value)) {
        ins(Opcode::ConstSampler, 0, plan_.pools.addSampler(*sampler));
    } else if (const auto* distribution = std::get_if<Distribution>(&value)) {
        if (distribution->points) {
            ins(Opcode::ConstDistribution, 0, plan_.pools.addDistribution(*distribution));
            return;
        }
        const auto count = distribution->family == DistributionFamily::Gaussian ? 2u :
            distribution->family == DistributionFamily::Triangular ? 3u : 4u;
        for (std::size_t i = 0; i < count; ++i) emitConst(distribution->parameters[i]);
        ins(distribution->family == DistributionFamily::Gaussian ? Opcode::DistributionGaussian :
            distribution->family == DistributionFamily::Triangular ? Opcode::DistributionTriangular : Opcode::DistributionBeta);
    }
}

TemporaryLocals::Lease Emitter::temporary(ips::ValueType type) {
    try {
        if (type.needsDescriptor()) { plan_.pools.addType(type); return temporaries_.acquire(type); }
        switch (type.kind) {
        case ips::ValueType::I32: return temporaries_.acquire(ValueType::I32);
        case ips::ValueType::F32: return temporaries_.acquire(ValueType::F32);
        case ips::ValueType::Vec2: return temporaries_.acquire(ValueType::Vec2);
        case ips::ValueType::Color: return temporaries_.acquire(ValueType::Color);
        case ips::ValueType::Distribution: return temporaries_.acquire(ValueType::Distribution);
        default: throw std::logic_error("temporary requires a value type");
        }
    } catch (const std::length_error& exception) {
        error({currentLine_, 1}, exception.what());
    }
}

bool Emitter::isParam(const std::string& name) const {
    if (current_->drawLike && (name == "v" || name == "a")) return true;
    for (const auto& param : current_->ast->params) {
        if (param.name == name) {
            return true;
        }
    }
    return false;
}

void Emitter::emitFunction(PlannedFunction& function) {
    current_ = &function;
    for (const auto* slots : {&function.params, &function.results, &function.locals})
        for (const auto type : *slots) if (type.needsDescriptor()) plan_.pools.addType(type);
    code_.clear();
    lines_.clear();
    currentLine_ = 0;
    temporaries_.reset(function.params.size(), function.locals);
    labelKinds_.clear();
    loopLabels_.clear();
    forCount_ = 0;
    for (const auto& stmt : function.ast->body) {
        emitStmt(stmt);
    }
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
    } else if (const auto* capture = std::get_if<ips::LayerStmt>(&stmt)) {
        ins(Opcode::LayerBegin, static_cast<u8>(capture->alpha),
            static_cast<u16>(current_->slots.at(capture->name)));
        for (const auto& child : capture->body) emitStmt(child);
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
        if (emitUnrolled(*forStmt, counter)) return;
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
                current_->typeOf(*ret->value) == ips::ValueType::I32) {
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
    return plan_.partSlot(current_->partOwner.value(), output.name);
}

void Emitter::emitOutputWrite(const ips::Program* owner,
                              const ips::OutputDecl& output,
                              const ips::Expr& value) {
    const u32 slot = slotForOutput(owner, output);
    if (output.kind == ips::OutputKind::Point) {
        // Points take the vec2 shape: a literal pair, a bare part read or
        // an unpacked vec2 value (see emitPointValue).
        emitPointValue(value);
        ins(output.isList ? Opcode::OutListPoint : Opcode::OutPoint, 0,
            static_cast<u16>(slot));
        return;
    }
    if (const auto* vector = std::get_if<ips::VectorLit>(&value)) {
        for (const auto& element : vector->elements) {
            emitExpr(*element);
            if (current_->typeOf(*element) == ips::ValueType::I32) {
                ins(Opcode::I32ToF32);
            }
        }
    } else {
        emitExpr(value); // part.get.* pushes the right shape
    }
    switch (output.kind) {
    case ips::OutputKind::Point:
        break; // handled above
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
