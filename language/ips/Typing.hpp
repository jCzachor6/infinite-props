#pragma once

// Shared expression typing for the .ips front end. Both the semantic
// checker and the compiler drive one Typer, so the type rules and the
// builtin signatures live in exactly one place. typeOf() validates as it
// types and throws SourceError at the location of the first problem.

#include <functional>
#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "ips/Ast.hpp"
#include "ips/Types.hpp"
#include "ips/BuiltinSpec.hpp"
#include "ips/Palette.hpp"

namespace iprop::ips {

// The declared surface of an imported part: its uniforms (with types) and
// structured output slots, as lightweight value types.
struct PartInput {
    InputType type = InputType::Color;
    std::string name;
};

struct PartOutput {
    OutputKind kind = OutputKind::Point;
    bool isList = false;
    std::string name;
};

struct PartInfo {
    std::vector<PartInput> inputs;
    std::vector<PartOutput> outputs;
};

// The declared parameter types of a function, in declaration order (the
// uniforms of a draw-like function are not part of its callable surface).
std::vector<ValueType> declaredParamTypes(const Function& function);

// What the owner of a Typer knows about a user function: its declared
// parameter types and its (possibly not-yet-known) return type.
struct FunctionSurface {
    std::vector<ValueType> params;
    std::optional<ValueType> result;
};

class Typer {
public:
    using ExpressionTypes = std::unordered_map<const Expr*, ValueType>;
    explicit Typer(const std::map<std::string, PartInfo>& parts);

    // ---- scope ------------------------------------------------------------
    //
    // The owner program (entry asset or part) whose inputs are in scope, the
    // function being typed (its params become i32 locals) and the locals
    // declared so far. enterFunction resets the locals; declareLocal grows
    // them as the walk progresses. Lazy checking of another function saves
    // and restores the scope (saveScope/restoreScope).

    struct Scope {
        const Program* owner = nullptr;
        bool isDraw = false;
        // The part alias prefix of the function being typed ("" for the
        // entry asset): bare call names resolve within it first.
        std::string ns;
        std::map<std::string, ValueType> locals;
        ExpressionTypes expressions;
    };

    void enterFunction(const Program& owner, const Function& function,
                       bool isDraw, std::string ns = "");
    void declareLocal(const std::string& name, ValueType type);

    Scope saveScope() const;
    void restoreScope(Scope scope);

    const std::map<std::string, ValueType>& locals() const { return locals_; }

    // Resolves a user function to its surface, lazily checking it on first
    // use. The provider returns nullopt for undefined names and throws for
    // problems of its own (e.g. recursion), using the supplied location. The
    // Typer reports "undefined function" itself.
    std::function<std::optional<FunctionSurface>(const std::string&,
                                                 const SourceLocation&,
                                                 const std::string&)>
        resolveFunction;

    // ---- typing -----------------------------------------------------------

    // The type of an expression, validating it along the way.
    ValueType typeOf(const Expr& expr);
    const ExpressionTypes& expressions() const { return expressions_; }

private:
    ExpressionTypes expressions_;
    ValueType infer(const Expr& expr);
    std::map<const PaletteDecl*, ResolvedPalette> palettes_;
    const ResolvedPalette& palette(const Identifier& name);
    const std::map<std::string, PartInfo>& parts_;

    const Program* owner_ = nullptr;
    bool isDraw_ = false;
    std::string ns_;
    std::map<std::string, ValueType> locals_;

    [[noreturn]] void error(const SourceLocation& at, std::string message) const;

    ValueType typeLiteral(const Literal& literal);
    ValueType typeIdentifier(const Identifier& identifier);
    ValueType typePartRead(const Expr& expr);
    ValueType typeCall(const Call& call);
    ValueType typeBuiltinCall(const Identifier& name, const Call& call,
                              const BuiltinSpec& signature);
    ValueType typeUserCall(const Identifier& name, const Call& call);
    ValueType typePartCall(const Call& call);
    ValueType typeVector(const VectorLit& vector);
    ValueType typeIndex(const Index& index);
    ValueType typeRamp(const Identifier& name, const Call& call);
    ValueType typeBinary(const Binary& binary);
    ValueType typeUnary(const Unary& unary);

    struct PartRead {
        std::string alias;
        const PartOutput* output = nullptr;
        OutputKind kind = OutputKind::Point;
    };
    PartRead partReadInfo(const Expr& expr) const;

    struct Path {
        std::string base; // empty if the base is not an identifier
        std::vector<std::string> segments; // outermost first
        SourceLocation location;
    };
    Path flatten(const Expr& expr) const;
};

} // namespace iprop::ips
