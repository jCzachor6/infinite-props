#pragma once

// Shared expression typing for the .ips front end. Both the semantic
// checker and the compiler drive one Typer, so the type rules and the
// builtin signatures live in exactly one place. typeOf() validates as it
// types and throws SourceError at the location of the first problem.

#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include "ips/Ast.hpp"
#include "ips/Types.hpp"
#include "ips/BuiltinSpec.hpp"
#include "ips/Palette.hpp"
#include "ips/CallPlan.hpp"
#include "ips/AssetConstants.hpp"

namespace iprop::ips {

// The declared surface of an imported part: its uniforms (with types) and
// structured output slots, as lightweight value types.
struct PartInput {
    InputType type = InputType::Color;
    std::string name;
    const InputDecl* declaration = nullptr;
};

struct PartOutput {
    OutputKind kind = OutputKind::Point;
    bool isList = false;
    std::string name;
};

// An asset constant of an import, which a render asset reads as
// `alias.name` (models v1.2, shared layout): its value is folded.
struct PartConstant {
    std::string name;
    ValueType type;
};

struct PartInfo {
    std::vector<PartInput> inputs;
    std::vector<PartOutput> outputs;
    std::vector<PartConstant> constants;
    // The import is a render asset (models v1.2); a model cannot use one as
    // a skin and 3D parts do not exist yet.
    bool model = false;
};

// The declared parameter types of a function, in declaration order (the
// uniforms of a draw-like function are not part of its callable surface).
std::vector<ValueType> declaredParamTypes(const Function& function);
std::vector<std::string> declaredParamNames(const Function& function);

// What the owner of a Typer knows about a user function: its declared
// parameter types and its (possibly not-yet-known) return type.
struct FunctionSurface {
    std::vector<ValueType> params;
    std::optional<ValueType> result;
    std::vector<std::string> names{};
};

// Recovery-only control flow: a failed local declaration has no usable type.
// Propagate that uncertainty without manufacturing a runtime value type or
// another diagnostic at every dependent expression. Strict typing never
// installs invalid locals and therefore never throws this marker.
struct UnresolvedType {};

class Typer {
public:
    using ExpressionTypes = std::unordered_map<const Expr*, ValueType>;
    // What typing decided about an expression beyond its type, so that later
    // stages never re-derive it.
    struct Resolution {
        const PaletteDecl* palette = nullptr;      // a bare palette list or legacy palette index
        const BuiltinOverload* overload = nullptr; // the overload of a method expression
    };
    using ExpressionResolutions = std::unordered_map<const Expr*, Resolution>;
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
        // The part alias path prefix of the function being typed ("" for
        // the entry asset): call names and part aliases resolve within it.
        std::string ns;
        std::map<std::string, ValueType> locals;
        ExpressionTypes expressions;
        std::set<std::string> invalidLocals{};
        CallPlans calls{};
        ExpressionResolutions resolutions{};
    };

    void enterFunction(const Program& owner, const Function& function,
                       bool isDraw, std::string ns = "");
    void declareLocal(const std::string& name, ValueType type);
    void declareInvalidLocal(const std::string& name);

    Scope saveScope() const;
    void restoreScope(Scope scope);

    const std::map<std::string, ValueType>& locals() const { return locals_; }
    // The member expressions read as an import's constant (`alias.name` in a
    // render asset): the planner folds exactly these.
    const std::set<const Expr*>& importConstantReads() const { return importConstantReads_; }

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
    ValueType typeOf(const Expr& expr, std::optional<ValueType> context = {});
    ValueType paletteElementType(const PaletteDecl& declaration);
    void registerPaletteType(const PaletteDecl& declaration, ValueType type);
    // Registers the resolved declarations of `owner`: its palette types and
    // the one constant probe that rejects statically known invalid values in
    // its runtime expressions. Expressions of other owners are not probed.
    void registerAssetDeclarations(const Program& owner, const AssetDeclarations& declarations);
    const ExpressionTypes& expressions() const { return expressions_; }
    const CallPlans& calls() const { return calls_; }
    const ExpressionResolutions& resolutions() const { return resolutions_; }
    // The palette a typed expression reads by name or legacy index, if any.
    const PaletteDecl* paletteRead(const Expr& expr) const;

private:
    ExpressionTypes expressions_;
    CallPlans calls_;
    ExpressionResolutions resolutions_;
    std::map<const Program*, std::shared_ptr<ConstantProbe>> probes_;
    std::optional<Value> known(const Expr& expr);
    std::optional<std::string> rejection(const Expr& call);
    [[noreturn]] void valueError(const SourceLocation& at, std::string message) const;
    void checkKnownPoints(const PointDistributionLiteral& literal);
    void checkKnownBounds(const SamplerCapture& capture, ValueType item);
    void checkKnownRange(const RandomRange& range, ValueType item, const SourceLocation& at);
    // Return a stable value: typing a nested helper may save/replace Scope.
    BoundCallPlan bind(const Call& call, CallSignature signature, std::string_view name);
    ValueType infer(const Expr& expr);
    std::map<const PaletteDecl*, ResolvedPalette> palettes_;
    std::set<const PaletteDecl*> activePalettes_;
    const ResolvedPalette& palette(const Identifier& name);
    // Parts are keyed by alias path; the current namespace prefixes a
    // source alias, so each asset sees only its direct imports.
    const std::map<std::string, PartInfo>& parts_;
    const PartInfo* findPart(const std::string& alias) const;
    std::vector<std::string> partAliases() const;

    const Program* owner_ = nullptr;
    bool isDraw_ = false;
    std::string ns_;
    std::map<std::string, ValueType> locals_;
    std::set<std::string> invalidLocals_;
    std::set<const Expr*> importConstantReads_;

    [[noreturn]] void error(const SourceLocation& at, std::string message) const;

    ValueType typeLiteral(const Literal& literal);
    ValueType typeIdentifier(const Expr& expr, const Identifier& identifier);
    ValueType typePartRead(const Expr& expr);
    ValueType typeCall(const Call& call);
    ValueType typeBuiltinCall(const Identifier& name, const Call& call,
                              const BuiltinSpec& signature);
    ValueType typeUserCall(const Identifier& name, const Call& call);
    ValueType typePartCall(const Call& call);
    // `alias.draw(seed, inputs..., size = {w, h})` in a render asset: the
    // imported draw asset generated as a texture (models v1.2).
    ValueType typeSkinCall(const Call& call);
    ValueType typeModelPartCall(const Call& call);
    // Whether the asset being typed has `render` in place of `draw`.
    bool modelAsset() const;
    ValueType typeVector(const VectorLit& vector);
    ValueType typeList(const ListLit& list, std::optional<ValueType> context);
    ValueType typeIndex(const Expr& expr, const Index& index);
    ValueType typeRamp(const Identifier& name, const Call& call);
    ValueType typeBinary(const Binary& binary);
    ValueType typeUnary(const Unary& unary);
    ValueType typeRandomRange(const RandomRange& range);
    ValueType typeListMethod(const Expr& expr, const ListMethod& method, std::optional<ValueType> context);
    ValueType typeBlur(const Expr& expr, const SurfaceBlur& blur);
    ValueType typeNormal(const Expr& expr, const SurfaceNormal& normal);
    // Named seeds (v1.1): `.fork`, and the operators a seed allows.
    ValueType typeFork(const SeedFork& fork);
    ValueType typeSeedOperands(const Binary& binary, ValueType left, ValueType right) const;
    [[noreturn]] void failSeedOperand(const Unary& unary) const;
    ValueType typeMember(const Expr& expr, const Member& member);
    ValueType typePoints(const PointDistributionLiteral& literal);
    ValueType typeCapture(const SamplerCapture& capture);
    ValueType typePick(const Expr& expr, const SamplerPick& pick);
    ValueType typeMemberCall(const Call& call);
    void checkKnownConstructor(const Expr& expr, const Call& call);
    [[noreturn]] void failUndefinedFunction(const Identifier& name, const Call& call) const;
    [[noreturn]] void failArgument(const Expr& argument, std::size_t index, const std::string& function,
                                   ValueType expected, ValueType got) const;
    [[noreturn]] void failBuiltinArgument(const Expr& argument, std::size_t index, const std::string& function,
                                          const BuiltinParameter& parameter, ValueType got) const;
    static std::string seedHint(ValueType expected, ValueType got);
    void checkSeedArgument(const Expr& argument, const char* what);
    [[noreturn]] void failSeedChoice(const Call& call, const std::string& function) const;
    [[noreturn]] void failInitialReceiver(const Call& call) const;
    bool namesValue(const std::string& name) const;
    static std::string methodNamespace(ValueType type);

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
