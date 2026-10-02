#include "Emit.hpp"

namespace iprop::compiler {
namespace {
bool safeBody(const std::vector<ips::Stmt>& body, const std::string& counter) {
    for (const auto& stmt : body) {
        if (const auto* local = std::get_if<ips::VarDecl>(&stmt)) {
            if (local->name == counter) return false;
        } else if (const auto* branch = std::get_if<ips::IfStmt>(&stmt)) {
            if (!safeBody(branch->thenBranch, counter) || !safeBody(branch->elseBranch, counter)) return false;
        } else if (const auto* loop = std::get_if<ips::WhileStmt>(&stmt)) {
            if (!safeBody(loop->body, counter)) return false;
        } else if (std::holds_alternative<ips::ForStmt>(stmt) ||
                   std::holds_alternative<ips::BreakStmt>(stmt) || std::holds_alternative<ips::ContinueStmt>(stmt)) {
            // Nested for-loops own planned hidden bound slots, and branch
            // depths refer to real labels. Keep their normal lowering.
            return false;
        }
    }
    return true;
}
} // namespace

bool Emitter::emitUnrolled(const ips::ForStmt& loop, u16 counter) {
    if (!unrollLoops_ || !safeBody(loop.body, loop.variable)) return false;
    const auto& from = current_->expression(*loop.from).constant;
    const auto& to = current_->expression(*loop.to).constant;
    if (!from || !to || !std::holds_alternative<i32>(*from) || !std::holds_alternative<i32>(*to)) return false;
    const auto first = std::get<i32>(*from), end = std::get<i32>(*to);
    const auto iterations = static_cast<std::int64_t>(end) - first;
    if (iterations > 8) return false;
    for (std::int64_t i = first; i < end; ++i) {
        currentLine_ = static_cast<u32>(loop.location.line);
        emitConst(static_cast<i32>(i));
        ins(Opcode::LocalSet, 0, counter);
        for (const auto& stmt : loop.body) emitStmt(stmt);
    }
    // Loop locals remain visible after the loop, including empty ranges.
    currentLine_ = static_cast<u32>(loop.location.line);
    emitConst(iterations > 0 ? end : first);
    ins(Opcode::LocalSet, 0, counter);
    return true;
}
} // namespace iprop::compiler
