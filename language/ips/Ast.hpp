#pragma once

// The .ips abstract syntax tree. Produced by the parser, consumed by the
// semantic analysis (name resolution, type checking) and the code generator.
//
// Design notes:
//  * A Program is a list of imports followed by exactly one asset block.
//  * Blocks are statement lists; statements never produce values (matching
//    the bytecode VM, where blocks have no result).
//  * Value selection is expressed via assignments to locals (`w = ...`),
//    never via block results.
//  * Expressions are a std::variant; recursive parts own their children via
//    unique_ptr.

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "ips/SourceError.hpp"

namespace iprop::ips {

// ---- Expressions ---------------------------------------------------------

// The recursive expression type. `Expr` inherits from the variant so it can
// be forward-declared (the member structs below hold unique_ptr<Expr>);
// std::get_if/std::visit work through the public variant base.
struct Expr;

// A literal value: the kind plus the exact lexeme (Int/Float/Color/String).
struct Literal {
    enum class Kind { Int, Float, Color, String, True, False };
    Kind kind = Kind::Int;
    std::string text;
    SourceLocation location;
};

// A bare name: `seed`, `v`, `a`, `palette`, `w`, ...
struct Identifier {
    std::string name;
    SourceLocation location;
};

// A dotted access: `eyes.outputs.pupil`. Chained member accesses nest:
// ((eyes . outputs) . pupil).
struct Member {
    std::unique_ptr<Expr> object;
    std::string name;
    SourceLocation location;
};

// One call argument: either positional or named (`at = {32, 4}`).
struct Arg {
    std::optional<std::string> name;
    std::unique_ptr<Expr> value;
    SourceLocation location;
};

// A call: callee is an Identifier or Member expression, e.g. `range(21, 31)`
// or `hair.draw(seed ^ 0x9e3779b9, v, a, at = {32, 4})`.
struct Call {
    std::unique_ptr<Expr> callee;
    std::vector<Arg> args;
    SourceLocation location;
};

// A vector literal: `{32, 4}` or `{96 - 20, 170 - 23, 40, 23}`.
struct VectorLit {
    std::vector<std::unique_ptr<Expr>> elements;
    SourceLocation location;
};

// An index into an asset-level palette: `palette[i]`.
struct Index {
    std::unique_ptr<Expr> object;
    std::unique_ptr<Expr> index;
    SourceLocation location;
};

struct Binary {
    enum class Op {
        Add, Sub, Mul, Div, Mod,
        Eq, Ne, Lt, Le, Gt, Ge,
        And, Or,        // && ||
        Xor, BitAnd, BitOr, // ^ & |
        Shl, Shr,       // << >>
    };
    Op op = Op::Add;
    std::unique_ptr<Expr> left;
    std::unique_ptr<Expr> right;
    SourceLocation location;
};

struct Unary {
    enum class Op { Neg, Not, BitNot };
    Op op = Op::Neg;
    std::unique_ptr<Expr> operand;
    SourceLocation location;
};

struct Expr
    : std::variant<Literal, Identifier, Member, Call, VectorLit, Index, Binary,
                   Unary> {
    using std::variant<Literal, Identifier, Member, Call, VectorLit, Index,
                       Binary, Unary>::variant;
};

// ---- Statements ----------------------------------------------------------

struct Stmt;

// `w = range(21, 31)` — declares a local and assigns it in one step.
struct VarDecl {
    std::string name;
    std::unique_ptr<Expr> value;
    SourceLocation location;
};

// A bare expression statement: a call, e.g. `face(v, a)` or
// `hair.draw(...)`.
struct ExprStmt {
    std::unique_ptr<Expr> expr;
    SourceLocation location;
};

struct IfStmt {
    std::unique_ptr<Expr> condition;
    std::vector<Stmt> thenBranch;
    std::vector<Stmt> elseBranch;
    SourceLocation location;
};

struct WhileStmt {
    std::unique_ptr<Expr> condition;
    std::vector<Stmt> body;
    SourceLocation location;
};

// `return` with an optional value. A null value means a bare `return`.
struct ReturnStmt {
    std::unique_ptr<Expr> value;
    SourceLocation location;
};

// `break` / `continue` — only valid inside a `while` body (checked in the
// semantics pass); the compiler lowers them to `br` on the loop's labels.
struct BreakStmt {
    SourceLocation location;
};

struct ContinueStmt {
    SourceLocation location;
};

// `for i in a..b { ... }` — an i32 counter loop over [a, b). The bounds are
// evaluated once and the counter increments at the end of each pass; sugar
// over the VM's block/loop/br (see the compiler lowering).
struct ForStmt {
    std::string variable;
    std::unique_ptr<Expr> from;
    std::unique_ptr<Expr> to;
    std::vector<Stmt> body;
    SourceLocation location;
};

struct Stmt
    : std::variant<VarDecl, ExprStmt, IfStmt, WhileStmt, ReturnStmt,
                   BreakStmt, ContinueStmt, ForStmt> {
    using std::variant<VarDecl, ExprStmt, IfStmt, WhileStmt, ReturnStmt,
                       BreakStmt, ContinueStmt, ForStmt>::variant;
};

// ---- Declarations --------------------------------------------------------

enum class InputType { Color, F32, U32, I32 };

enum class OutputKind { Point, Rect, Circle };

// `input color palette = #808d74`
struct InputDecl {
    InputType type = InputType::Color;
    std::string name;
    std::unique_ptr<Expr> defaultValue;
    SourceLocation location;
};

// `output rect collider` or `output list point pts`
struct OutputDecl {
    OutputKind kind = OutputKind::Point;
    bool isList = false;
    std::string name;
    SourceLocation location;
};

// `palette name = [ e, e, ... ]` — asset-level constant array data. The
// elements are expressions (the checker requires them constant); the
// compiler desugars `name[i]` into an if-ladder with the last element as
// the default.
struct PaletteDecl {
    std::string name;
    std::vector<std::unique_ptr<Expr>> elements;
    SourceLocation location;
};

// The declared type of a function parameter or return value: the three
// scalar types the VM can store in locals (spec §3/§9). Unlike `input`,
// there is no u32 — parameters are always locals.
enum class ParamType { I32, F32, Color };

// One function parameter: a name plus its declared type. Pre-1.0 the type
// is mandatory — untyped parameters are rejected (the language is not
// backward compatible until the 1.0 freeze).
struct Param {
    std::string name;
    ParamType type;
    SourceLocation location;
};

// A function (the entry point is named `draw` and takes `(v, a)`).
struct Function {
    std::string name;
    std::vector<Param> params;
    std::optional<ParamType> returnType; // declared with `-> type`
    std::vector<Stmt> body;
    SourceLocation location;
};

// `import hair from "hair.ips"`
struct Import {
    std::string alias;
    std::string path;
    SourceLocation location;
};

// One asset block.
struct Asset {
    std::string name;
    SourceLocation location;

    std::optional<std::uint32_t> sizeX;
    std::optional<std::uint32_t> sizeY;
    std::unique_ptr<Expr> originX;
    std::unique_ptr<Expr> originY;
    std::optional<std::uint32_t> variants;
    std::optional<std::uint32_t> archetypes;

    std::vector<InputDecl> inputs;
    std::vector<OutputDecl> outputs;
    std::vector<PaletteDecl> palettes;
    std::vector<Function> functions;
};

struct Program {
    std::vector<Import> imports;
    Asset asset;
};

// The source location of any expression node (for semantic diagnostics).
inline SourceLocation locationOf(const Expr& expr) {
    return std::visit([](const auto& node) { return node.location; }, expr);
}

} // namespace iprop::ips
