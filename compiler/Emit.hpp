#pragma once

// The lowering layer: turns checked ASTs into the instruction IR
// (iprop::Instruction) recorded in the plan. Expression types and pure
// constants are already resolved; the emitter allocates pool entries as it
// goes. It mutates only the current function's code/locals and the plan's
// constant pools; the planning tables are read-only.

#include <string>
#include <vector>

#include "iprop/Instruction.hpp"
#include "iprop/Value.hpp"
#include "ips/Ast.hpp"
#include "ips/Types.hpp"
#include "Plan.hpp"
#include "TemporaryLocals.hpp"

namespace iprop::compiler {

class Emitter {
public:
    // `plan` must be fully planned (every function prepared); it is not
    // modified except for the constant pools and the function being emitted.
    explicit Emitter(Plan& plan, bool unrollLoops = false);

    // Emits one function's body into `function.code`, acquiring typed
    // temporary locals only for values whose lifetimes overlap.
    void emitFunction(PlannedFunction& function);

private:
    friend class PaletteLowerer;
    Plan& plan_;
    bool unrollLoops_ = false;
    PlannedFunction* current_ = nullptr;
    std::vector<Instruction> code_; // the IR of the function being emitted
    std::vector<u32> lines_;        // the .ips source line per instruction
    u32 currentLine_ = 0;           // the line attributed to new instructions
    TemporaryLocals temporaries_;

    [[noreturn]] void error(const ips::SourceLocation& at, std::string message);

    // ---- shared helpers -----------------------------------------------------

    void ins(Opcode opcode, u8 aux = 0, u16 operand = 0);
    void emitConst(const Value& value);
    TemporaryLocals::Lease temporary(ips::ValueType type);
    bool isParam(const std::string& name) const;
    u32 slotForOutput(const ips::Program* owner,
                      const ips::OutputDecl& output) const;
    void emitOutputWrite(const ips::Program* owner,
                         const ips::OutputDecl& output, const ips::Expr& value);

    // The runtime label kinds of the open control frames, outermost first
    // (0 = block, 1 = loop). `break`/`continue` lower to `br` on the
    // enclosing while's block/loop label; the depth is the distance from
    // the innermost open label.
    std::vector<u8> labelKinds_;
    // The {block, loop} label indices of each open while, innermost last.
    std::vector<std::pair<u32, u32>> loopLabels_;
    // Counts the emitted for-loops to name their hidden bound locals; the
    // Planner walks the same statements in the same order.
    u32 forCount_ = 0;

    // ---- statements (Emit.cpp) ----------------------------------------------

    void emitStmt(const ips::Stmt& stmt);
    bool emitUnrolled(const ips::ForStmt& loop, u16 counter);

    // ---- expressions (EmitExpr.cpp / EmitCall.cpp) ---------------------------

    ips::ValueType emitExpr(const ips::Expr& expr);
    void emitPointValue(const ips::Expr& expr);
    ips::ValueType emitPartRead(const ips::Expr& expr);
    ips::ValueType emitBinary(const ips::Binary& bin);
    ips::ValueType emitUnary(const ips::Unary& un);
    ips::ValueType emitCall(const ips::Call& call);
    void emitArg(const PlannedArgument& arg);
    // The instruction of a resolved overload whose arguments are already
    // on the stack; a type-descriptor operand names its first parameter.
    void emitOverload(const ips::BuiltinOverload& overload);
    // A method expression (blur, list method, pick) lowered through the
    // overload the Typer selected.
    ips::ValueType emitMethod(const ips::Expr& expr);
    ips::ValueType emitBuiltin(const BuiltinCall& call);
    ips::ValueType emitSelection(const SelectionCall& call);
    void emitPartCall(const PartCall& call);

    struct PartRead {
        std::string alias; // the instance's alias path
        const ips::OutputDecl* output = nullptr;
        ips::OutputKind kind = ips::OutputKind::Point;
    };
    PartRead partReadInfo(const ips::Expr& expr) const;
};

} // namespace iprop::compiler
