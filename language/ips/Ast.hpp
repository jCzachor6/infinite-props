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

#include "ips/Lexer.hpp"
#include "iprop/TypeDescriptor.hpp"
#include "iprop/Compositor.hpp"

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

struct ListLit {
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

// A random draw: `lower to upper [with distribution]`. Not a value range:
// every evaluation draws again from the current stream. The location is the
// `to` token.
struct RandomRange {
    std::unique_ptr<Expr> lower;
    std::unique_ptr<Expr> upper;
    SourceLocation location;
    std::unique_ptr<Expr> distribution{}; // null: uniform
    SourceLocation withLocation{};
};

// Capture changes only the outer range into a description. Nested ranges in
// bounds remain eager expressions; this is neither a lambda nor a lazy call.
struct SamplerCapture {
    std::unique_ptr<Expr> value;
    SourceLocation location;
    bool stepper = false; // same eager capture syntax; distinct description type
};
struct SamplerPick {
    std::unique_ptr<Expr> receiver;
    std::unique_ptr<Expr> count; // null: one scalar result; otherwise list<T>
    SourceLocation location;
};
struct ListMethod {
    std::unique_ptr<Expr> receiver;
    std::string name; // distinct or sort; never a global keyword
    SourceLocation location;
};
struct SurfaceBlur {
    std::unique_ptr<Expr> receiver;
    Arg sigma;
    SourceLocation location;
};

struct PointDistributionLiteral {
    struct Point {
        std::unique_ptr<Expr> key, value;
        SourceLocation location;
    };
    DistributionFamily family = DistributionFamily::Weighted;
    std::vector<Point> points;
    SourceLocation location;
};

struct Expr
    : std::variant<Literal, Identifier, Member, Call, VectorLit, ListLit, Index, Binary,
                   Unary, RandomRange, SamplerCapture, SamplerPick, ListMethod, SurfaceBlur, PointDistributionLiteral> {
    using std::variant<Literal, Identifier, Member, Call, VectorLit, ListLit, Index,
                       Binary, Unary, RandomRange, SamplerCapture, SamplerPick, ListMethod, SurfaceBlur, PointDistributionLiteral>::variant;
    TokenRange sourceRange{};
    TokenRange sourceCoreRange{}; // excludes redundant outer parentheses
};

// ---- Statements ----------------------------------------------------------

// The declared type of a function parameter or return value: the scalar
// and vec2 types the VM can store in locals (spec §3/§9). Unlike `input`,
// there is no u32 — parameters are always locals.
using ParamType = iprop::TypeDescriptor;

struct Stmt;

// `w = range(21, 31)` — declares a local and assigns it in one step.
// `distribution d = gaussian(...)` also states the type.
struct VarDecl {
    std::string name;
    std::unique_ptr<Expr> value;
    SourceLocation location;
    std::optional<ParamType> declaredType{};
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
    TokenRange elseRange{};
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

// A capture statement. Its result binds only after the body exits;
// the body executes on a fresh target without automatically painting its parent.
struct LayerStmt {
    std::string name;
    CompositionMode alpha = CompositionMode::Inherited;
    std::vector<Stmt> body;
    SourceLocation location;
};
struct ClipStmt {
    std::unique_ptr<Expr> mask;
    std::vector<Stmt> body;
    SourceLocation location;
};

struct Stmt
    : std::variant<VarDecl, ExprStmt, IfStmt, WhileStmt, ReturnStmt,
                   BreakStmt, ContinueStmt, ForStmt, LayerStmt, ClipStmt> {
    using std::variant<VarDecl, ExprStmt, IfStmt, WhileStmt, ReturnStmt,
                       BreakStmt, ContinueStmt, ForStmt, LayerStmt, ClipStmt>::variant;
    TokenRange sourceRange{};
};

// ---- Declarations --------------------------------------------------------

enum class InputType { Color, F32, U32, I32 };

// Resource exports are source-level outputs, separate from the frozen host enum.
enum class OutputKind { Point, Rect, Circle, Mask, Layer };
constexpr bool isResourceOutput(OutputKind kind) {
    return kind == OutputKind::Mask || kind == OutputKind::Layer;
}

// `input color palette = #808d74`
struct InputDecl {
    InputType type = InputType::Color;
    std::string name;
    std::unique_ptr<Expr> defaultValue;
    SourceLocation location;
    SourceLocation typeLocation{};
    TokenRange sourceRange{};
};

// `output rect collider` or `output list point pts`
struct OutputDecl {
    OutputKind kind = OutputKind::Point;
    bool isList = false;
    std::string name;
    SourceLocation location;
    TokenRange sourceRange{};
};

// `palette name = [ e, e, ... ]` — asset-level constant array data. The
// elements are expressions (the checker requires them constant); the
// compiler desugars `name[i]` into a selection tree with the last element as
// the default.
struct PaletteDecl {
    std::string name;
    std::vector<std::unique_ptr<Expr>> elements;
    SourceLocation location;
    TokenRange sourceRange{};
};


// One function parameter: a name plus its declared type. Pre-1.0 the type
// is mandatory — untyped parameters are rejected (the language is not
// backward compatible until the 1.0 freeze).
struct Param {
    std::string name;
    ParamType type;
    SourceLocation location;
};

// A function. The draw entry has no declared parameters; its v/a runtime
// parameters are synthesized by the compiler. Helpers keep explicit types.
struct Function {
    std::string name;
    std::vector<Param> params;
    std::optional<ParamType> returnType; // declared with `-> type`
    std::vector<Stmt> body;
    SourceLocation location;
    TokenRange sourceRange{};
};

// `import hair from "hair.ips"`
struct Import {
    std::string alias;
    std::string path;
    SourceLocation location;
    TokenRange sourceRange{};
};

// One asset block.
struct Asset {
    std::string name;
    SourceLocation location;

    std::optional<std::uint32_t> sizeX;
    std::optional<std::uint32_t> sizeY;
    std::unique_ptr<Expr> originX;
    std::unique_ptr<Expr> originY;

    std::vector<InputDecl> inputs;
    std::vector<OutputDecl> outputs;
    std::vector<PaletteDecl> palettes;
    struct Constant {
        ParamType type;
        std::string name;
        std::unique_ptr<Expr> value;
        SourceLocation location;
        TokenRange sourceRange{};
    };
    std::vector<Constant> constants;
    std::vector<Function> functions;

    TokenRange sourceRange{};
    std::vector<MetadataSyntax> metadataSyntax{};
};

struct Program {
    std::vector<Import> imports;
    Asset asset;
    // Owning token/trivia snapshot; compiler and debug printers ignore it.
    std::vector<Token> sourceTokens{};
};

// The source location of any expression node (for semantic diagnostics).
inline SourceLocation locationOf(const Expr& expr) {
    return std::visit([](const auto& node) { return node.location; }, expr);
}

} // namespace iprop::ips
