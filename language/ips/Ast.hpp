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
};

// A bare name: `seed`, `v`, `a`, `palette`, `w`, ...
struct Identifier {
    std::string name;
};

// A dotted access: `eyes.outputs.pupil`. Chained member accesses nest:
// ((eyes . outputs) . pupil).
struct Member {
    std::unique_ptr<Expr> object;
    std::string name;
};

// One call argument: either positional or named (`at = {32, 4}`).
struct Arg {
    std::optional<std::string> name;
    std::unique_ptr<Expr> value;
};

// A call: callee is an Identifier or Member expression, e.g. `range(21, 31)`
// or `hair.draw(seed ^ 0x9e3779b9, v, a, at = {32, 4})`.
struct Call {
    std::unique_ptr<Expr> callee;
    std::vector<Arg> args;
};

// A vector literal: `{32, 4}` or `{96 - 20, 170 - 23, 40, 23}`.
struct VectorLit {
    std::vector<std::unique_ptr<Expr>> elements;
};

struct Binary {
    enum class Op {
        Add, Sub, Mul, Div, Mod,
        Eq, Ne, Lt, Le, Gt, Ge,
        And, Or,        // && ||
        Xor, BitAnd, BitOr, // ^ & |
    };
    Op op = Op::Add;
    std::unique_ptr<Expr> left;
    std::unique_ptr<Expr> right;
};

struct Unary {
    enum class Op { Neg, Not, BitNot };
    Op op = Op::Neg;
    std::unique_ptr<Expr> operand;
};

struct Expr
    : std::variant<Literal, Identifier, Member, Call, VectorLit, Binary,
                   Unary> {
    using std::variant<Literal, Identifier, Member, Call, VectorLit, Binary,
                       Unary>::variant;
};

// ---- Statements ----------------------------------------------------------

struct Stmt;

// `w = range(21, 31)` — declares a local and assigns it in one step.
struct VarDecl {
    std::string name;
    std::unique_ptr<Expr> value;
};

// A bare expression statement: a call, e.g. `face(v, a)` or
// `hair.draw(...)`.
struct ExprStmt {
    std::unique_ptr<Expr> expr;
};

struct IfStmt {
    std::unique_ptr<Expr> condition;
    std::vector<Stmt> thenBranch;
    std::vector<Stmt> elseBranch;
};

struct WhileStmt {
    std::unique_ptr<Expr> condition;
    std::vector<Stmt> body;
};

// `return` with an optional value. A null value means a bare `return`.
struct ReturnStmt {
    std::unique_ptr<Expr> value;
};

struct Stmt : std::variant<VarDecl, ExprStmt, IfStmt, WhileStmt, ReturnStmt> {
    using std::variant<VarDecl, ExprStmt, IfStmt, WhileStmt, ReturnStmt>::variant;
};

// ---- Declarations --------------------------------------------------------

enum class InputType { Color, F32, U32, I32 };

enum class OutputKind { Point, Rect, Circle };

// `input color palette = #808d74`
struct InputDecl {
    InputType type = InputType::Color;
    std::string name;
    std::unique_ptr<Expr> defaultValue;
};

// `output rect collider` or `output list point pts`
struct OutputDecl {
    OutputKind kind = OutputKind::Point;
    bool isList = false;
    std::string name;
};

// A function (the entry point is named `draw` and takes `(v, a)`).
struct Function {
    std::string name;
    std::vector<std::string> params;
    std::vector<Stmt> body;
};

// `import hair from "hair.ips"`
struct Import {
    std::string alias;
    std::string path;
};

// One asset block.
struct Asset {
    std::string name;

    std::optional<std::uint32_t> sizeX;
    std::optional<std::uint32_t> sizeY;
    std::unique_ptr<Expr> originX;
    std::unique_ptr<Expr> originY;
    std::optional<std::uint32_t> variants;
    std::optional<std::uint32_t> archetypes;

    std::vector<InputDecl> inputs;
    std::vector<OutputDecl> outputs;
    std::vector<Function> functions;
};

struct Program {
    std::vector<Import> imports;
    Asset asset;
};

} // namespace iprop::ips
