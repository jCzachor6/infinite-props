#include "ips/Semantics.hpp"
#include "ips/Palette.hpp"
#include "ips/DeclarationConstants.hpp"
#include "ips/AssetConstants.hpp"

#include <algorithm>
#include <optional>
#include <set>
#include <utility>

#include "ips/Typing.hpp"

namespace iprop::ips {
namespace {

ValueType outputValueType(OutputKind kind) {
    switch (kind) {
    case OutputKind::Point: return ValueType::Vec2;
    case OutputKind::Rect: return ValueType::Rect;
    case OutputKind::Circle: return ValueType::Circle;
    }
    return ValueType::Vec2;
}

// ---- the checker -----------------------------------------------------------

class Checker {
public:
    Checker(const Program& program, const std::map<std::string, PartInfo>& parts,
            std::vector<SourceError>& diagnostics, std::size_t limit)
        : program_(program), parts_(parts), typer_(parts), diagnostics_(diagnostics),
          limit_(std::clamp<std::size_t>(limit, 1, 20)) {
        typer_.resolveFunction = [this](const std::string& name,
                                        const SourceLocation& at,
                                        const std::string&) {
            return checkedFunctionSurface(name, at);
        };
    }

    void run() {
        if (!recover([&] { checkImports(); }) || !checkAssetDecls()) return;
        const Function* draw = findFunction("draw");
        recover([&] {
            if (draw == nullptr)
                error(program_.asset.location, "missing 'draw { ... }' entry block");
            if (!draw->params.empty())
                error(draw->location, "'draw' takes no declared parameters; use 'draw { ... }'");
            if (draw->returnType.has_value())
                error(draw->location, "'draw' cannot return a value");
        });
        for (const auto& function : program_.asset.functions) {
            if (atLimit()) break;
            recover([&] { checkedFunctionSurface(function.name, function.location); });
        }
    }

private:
    const Program& program_;
    const std::map<std::string, PartInfo>& parts_;
    Typer typer_;
    std::vector<SourceError>& diagnostics_;
    std::size_t limit_;

    std::map<std::string, const Function*> functions_;
    std::map<std::string, std::optional<ValueType>> returnTypes_;
    std::set<std::string> inProgress_;

    // Per-function state.
    const Function* current_ = nullptr;
    std::uint32_t loopDepth_ = 0;

    [[noreturn]] void error(const SourceLocation& at, std::string message) {
        throw SourceError{at, std::move(message), {}, DiagnosticCode::TypeError,
                          DiagnosticStage::Typing};
    }

    bool atLimit() const { return diagnostics_.size() >= limit_; }

    template<class Check> bool recover(Check check) {
        if (atLimit()) return false;
        try { check(); return true; }
        catch (const SourceError& diagnostic) { diagnostics_.push_back(diagnostic); }
        catch (const UnresolvedType&) { /* dependent on an already reported failure */ }
        return false;
    }

    void checkRecoveringStmt(const Stmt& stmt) {
        const auto savedDepth = loopDepth_;
        if (recover([&] { checkStmt(stmt); })) return;
        loopDepth_ = savedDepth;
        markInvalidBindings(stmt);
    }

    void markInvalidLocal(const std::string& name) {
        // Failed writes and skipped declarations never poison a known input,
        // output, parameter or local. A later valid assignment can establish
        // the type of a recovery-only invalid local.
        if (!findInput(name) && !findOutput(name) && !findPalette(name) &&
            !findAssetConstant(program_, name) &&
            !isParam(name) && name != "seed" &&
            !(current_->name == "draw" && (name == "v" || name == "a")))
            typer_.declareInvalidLocal(name);
    }

    void markInvalidBindings(const Stmt& stmt) {
        if (const auto* decl = std::get_if<VarDecl>(&stmt)) markInvalidLocal(decl->name);
        else if (const auto* branch = std::get_if<IfStmt>(&stmt)) {
            for (const auto& child : branch->thenBranch) markInvalidBindings(child);
            for (const auto& child : branch->elseBranch) markInvalidBindings(child);
        } else if (const auto* whileLoop = std::get_if<WhileStmt>(&stmt)) {
            for (const auto& child : whileLoop->body) markInvalidBindings(child);
        } else if (const auto* forLoop = std::get_if<ForStmt>(&stmt)) {
            markInvalidLocal(forLoop->variable);
            for (const auto& child : forLoop->body) markInvalidBindings(child);
        } else if (const auto* capture = std::get_if<LayerStmt>(&stmt)) {
            markInvalidLocal(capture->name);
            for (const auto& child : capture->body) markInvalidBindings(child);
        }
    }

    // ---- declarations -------------------------------------------------------

    void checkImports() {
        std::set<std::string> seen;
        for (const auto& import : program_.imports) {
            if (import.alias == "surface")
                error(import.location, "import alias 'surface' conflicts with the surface namespace; rename the alias");
            if (!seen.insert(import.alias).second) {
                error(import.location,
                      "duplicate import alias '" + import.alias + "'");
            }
            if (parts_.find(import.alias) == parts_.end()) {
                error(import.location,
                      "imported part '" + import.alias + "' was not provided");
            }
        }
    }

    bool checkAssetDecls() {
        recover([&] {
            if (!program_.asset.sizeX.has_value())
                error(program_.asset.location, "missing 'size' declaration");
        });
        bool namespaceValid = true;

        std::set<std::string> inputNames;
        for (const auto& input : program_.asset.inputs) {
            if (atLimit()) return false;
            const bool valid = recover([&] {
                if (input.name == "seed" || input.name == "at" ||
                    input.name == "v" || input.name == "a" || input.name == "rot" ||
                    input.name == "mirror_x" || input.name == "mirror_y") {
                    error(input.location, "'" + input.name + "' is a reserved name");
                }
                if (!inputNames.insert(input.name).second) {
                    error(input.location, "duplicate input '" + input.name + "'");
                }
            });
            namespaceValid = valid && namespaceValid;
            recover([&] { checkInputDefault(input); });
        }

        std::set<std::string> outputNames;
        for (const auto& output : program_.asset.outputs) {
            if (atLimit()) return false;
            const bool valid = recover([&] {
                if (output.name == "v" || output.name == "a")
                    error(output.location, "'" + output.name + "' is a reserved name");
                if (!outputNames.insert(output.name).second) {
                    error(output.location, "duplicate output '" + output.name + "'");
                }
                if (inputNames.count(output.name) != 0) {
                    error(output.location, "output '" + output.name +
                                               "' collides with an input of the same name");
                }
            });
            namespaceValid = valid && namespaceValid;
        }

        for (const auto& function : program_.asset.functions) {
            if (atLimit()) return false;
            const bool valid = recover([&] {
                if (!functions_.emplace(function.name, &function).second) {
                    error(function.location,
                          "duplicate function '" + function.name + "'");
                }
                std::set<std::string> parameters;
                for (const auto& param : function.params) {
                    if (!parameters.insert(param.name).second)
                        error(param.location, "duplicate parameter '" + param.name + "'");
                }
            });
            namespaceValid = valid && namespaceValid;
        }

        std::set<std::string> paletteNames;
        bool paletteDeclarationsValid = true;
        for (const auto& palette : program_.asset.palettes) {
            if (atLimit()) return false;
            const bool valid = recover([&] {
                if (!paletteNames.insert(palette.name).second) {
                    error(palette.location, "duplicate palette '" + palette.name + "'");
                }
                if (inputNames.count(palette.name) != 0 ||
                    outputNames.count(palette.name) != 0) {
                    error(palette.location,
                          "palette '" + palette.name +
                              "' collides with an input or output of the same name");
                }
                if (functions_.count(palette.name) != 0) {
                    error(palette.location,
                          "palette '" + palette.name +
                              "' collides with a function of the same name");
                }
            });
            namespaceValid = valid && namespaceValid;
            if (valid) paletteDeclarationsValid = recover([&] { checkPaletteDecl(palette); }) && paletteDeclarationsValid;
        }
        // Do not report the same invalid legacy palette again when resolving
        // the shared declaration graph. Recovery already recorded its error.
        return paletteDeclarationsValid && recover([&] {
            const auto declarations = resolveAssetDeclarations(program_);
            for (const auto& [palette, value] : declarations.palettes) typer_.registerPaletteType(*palette, value.type);
        }) && namespaceValid;
    }

    void checkInputDefault(const InputDecl& input) {
        (void)resolveInputDefault(input);
    }

    void checkPaletteDecl(const PaletteDecl& palette) {
        if (palette.name == "seed" || palette.name == "at" ||
            palette.name == "v" || palette.name == "a") {
            error(palette.location, "'" + palette.name + "' is a reserved name");
        }
        if (!extendedPalette(palette)) (void)resolvePalette(palette);
    }

    // ---- functions ----------------------------------------------------------

    const Function* findFunction(const std::string& name) const {
        const auto found = functions_.find(name);
        return found == functions_.end() ? nullptr : found->second;
    }

    // Resolves a user function to its surface, checking the body lazily on
    // first use. Recursive calls are rejected; unknown names return nullopt
    // (the Typer reports them).
    std::optional<FunctionSurface> checkedFunctionSurface(
        const std::string& name, const SourceLocation& at) {
        if (returnTypes_.count(name) != 0) {
            return FunctionSurface{declaredParamTypes(*findFunction(name)),
                                   returnTypes_[name], declaredParamNames(*findFunction(name))};
        }
        const Function* function = findFunction(name);
        if (function == nullptr) {
            return std::nullopt;
        }
        if (inProgress_.count(name) != 0) {
            error(at, "recursive call to '" + name + "' is not supported");
        }
        inProgress_.insert(name);
        try { checkFunction(*function); }
        catch (...) { inProgress_.erase(name); throw; }
        inProgress_.erase(name);
        return FunctionSurface{declaredParamTypes(*function), returnTypes_[name], declaredParamNames(*function)};
    }

    void checkFunction(const Function& function) {
        // Lazy checking (a call to a not-yet-checked function) happens in
        // the middle of another function's body: save and restore the
        // per-function state.
        const Function* savedCurrent = current_;
        Typer::Scope savedScope = typer_.saveScope();
        const std::uint32_t savedLoopDepth = std::exchange(loopDepth_, 0);

        current_ = &function;
        try {
            typer_.enterFunction(program_, function, function.name == "draw");
            for (const auto& stmt : function.body) {
                if (atLimit()) break;
                checkRecoveringStmt(stmt);
            }
            returnTypes_[function.name] = function.returnType.has_value()
                                              ? std::optional<ValueType>(valueTypeOf(
                                                    *function.returnType))
                                              : std::nullopt;
        } catch (...) {
            current_ = savedCurrent;
            loopDepth_ = savedLoopDepth;
            typer_.restoreScope(std::move(savedScope));
            throw;
        }

        current_ = savedCurrent;
        loopDepth_ = savedLoopDepth;
        typer_.restoreScope(std::move(savedScope));
    }

    // ---- statements ---------------------------------------------------------

    void checkStmt(const Stmt& stmt) {
        if (const auto* decl = std::get_if<VarDecl>(&stmt)) {
            checkVarDecl(*decl);
        } else if (const auto* exprStmt = std::get_if<ExprStmt>(&stmt)) {
            (void)typer_.typeOf(*exprStmt->expr);
        } else if (const auto* ifStmt = std::get_if<IfStmt>(&stmt)) {
            checkCondition(*ifStmt->condition, ifStmt->location, "if");
            for (const auto& s : ifStmt->thenBranch) {
                if (atLimit()) break;
                checkRecoveringStmt(s);
            }
            for (const auto& s : ifStmt->elseBranch) {
                if (atLimit()) break;
                checkRecoveringStmt(s);
            }
        } else if (const auto* whileStmt = std::get_if<WhileStmt>(&stmt)) {
            checkCondition(*whileStmt->condition, whileStmt->location, "while");
            ++loopDepth_;
            for (const auto& s : whileStmt->body) {
                if (atLimit()) break;
                checkRecoveringStmt(s);
            }
            --loopDepth_;
        } else if (const auto* forStmt = std::get_if<ForStmt>(&stmt)) {
            checkFor(*forStmt);
        } else if (const auto* capture = std::get_if<LayerStmt>(&stmt)) {
            if (findOutput(capture->name) || findInput(capture->name) || findPalette(capture->name) ||
                findAssetConstant(program_, capture->name) || isParam(capture->name) || capture->name == "seed" ||
                (current_->name == "draw" && (capture->name == "v" || capture->name == "a")))
                error(capture->location, "layer result '" + capture->name + "' must be a writable local");
            for (const auto& child : capture->body) {
                if (atLimit()) break;
                checkRecoveringStmt(child);
            }
            const auto existing = typer_.locals().find(capture->name);
            if (existing != typer_.locals().end() && existing->second != ValueType::Layer)
                error(capture->location, "layer result '" + capture->name + "' conflicts with a local of type " + typeName(existing->second));
            typer_.declareLocal(capture->name, ValueType::Layer);
        } else if (const auto* ret = std::get_if<ReturnStmt>(&stmt)) {
            checkReturn(*ret);
        } else if (const auto* brk = std::get_if<BreakStmt>(&stmt)) {
            if (loopDepth_ == 0) {
                error(brk->location, "'break' outside a loop");
            }
        } else if (const auto* cont = std::get_if<ContinueStmt>(&stmt)) {
            if (loopDepth_ == 0) {
                error(cont->location, "'continue' outside a loop");
            }
        }
    }

    void checkFor(const ForStmt& forStmt) {
        for (const Expr* bound : {forStmt.from.get(), forStmt.to.get()}) {
            const ValueType type = typer_.typeOf(*bound);
            if (type != ValueType::I32) {
                error(locationOf(*bound),
                      "'for' bounds must be i32, got " +
                          std::string(typeName(type)));
            }
        }
        if (findAssetConstant(program_, forStmt.variable))
            error(forStmt.location, "cannot use const '" + forStmt.variable + "' as a loop counter");
        if (forStmt.variable == "seed" ||
            (current_->name == "draw" &&
             (forStmt.variable == "v" || forStmt.variable == "a"))) {
            error(forStmt.location, "'" + forStmt.variable + "' is a reserved name");
        }
        if (findOutput(forStmt.variable) != nullptr) {
            error(forStmt.location,
                  "'for' variable '" + forStmt.variable +
                      "' is an output (outputs cannot be loop counters)");
        }
        if (findInput(forStmt.variable) != nullptr) {
            error(forStmt.location,
                  "'for' variable '" + forStmt.variable +
                      "' is an input (inputs cannot be loop counters)");
        }
        if (findPalette(forStmt.variable) != nullptr) {
            error(forStmt.location,
                  "'for' variable '" + forStmt.variable +
                      "' is a palette (palettes cannot be loop counters)");
        }
        if (isParam(forStmt.variable)) {
            error(forStmt.location,
                  "cannot use parameter '" + forStmt.variable +
                      "' as a loop counter");
        }
        const auto& locals = typer_.locals();
        const auto existing = locals.find(forStmt.variable);
        if (existing == locals.end()) {
            typer_.declareLocal(forStmt.variable, ValueType::I32);
        } else if (existing->second != ValueType::I32) {
            error(forStmt.location, "local '" + forStmt.variable + "' is " +
                                         typeName(existing->second) +
                                         " but the loop counter is i32");
        }
        ++loopDepth_;
        for (const auto& s : forStmt.body) {
            if (atLimit()) break;
            checkRecoveringStmt(s);
        }
        --loopDepth_;
    }

    void checkReturn(const ReturnStmt& ret) {
        if (!ret.value) {
            if (current_->returnType.has_value()) {
                error(ret.location, "function '" + current_->name +
                                        "' declares a return type but 'return' "
                                        "has no value");
            }
            return;
        }
        if (current_->name == "draw") {
            error(ret.location, "'draw' cannot return a value");
        }
        const ValueType type = typer_.typeOf(*ret.value, current_->returnType);
        if (!isStorable(type)) {
            error(ret.location,
                  "cannot return " + std::string(typeName(type)) +
                      " (only scalars can be returned)");
        }
        if (!current_->returnType.has_value()) {
            error(ret.location, "function '" + current_->name +
                                    "' returns a value but declares no return "
                                    "type ('-> i32', '-> f32' or '-> color')");
        }
        const ValueType declared = valueTypeOf(*current_->returnType);
        if (!argFits(declared, type)) {
            error(ret.location, "return type of '" + current_->name + "' is " +
                                    typeName(declared) + ", got " +
                                    typeName(type));
        }
    }

    void checkCondition(const Expr& condition, const SourceLocation& at,
                        const char* what) {
        const ValueType type = typer_.typeOf(condition);
        if (type != ValueType::I32) {
            error(at, std::string(what) +
                          " condition must be an integer (i32), got " +
                          typeName(type));
        }
    }

    void checkVarDecl(const VarDecl& decl) {
        // Output writes: `collider = {...}` where collider is a declared slot.
        if (const OutputDecl* output = findOutput(decl.name)) {
            checkOutputWrite(*output, *decl.value);
            return;
        }
        if (findInput(decl.name) != nullptr) {
            error(decl.location, "cannot assign to input '" + decl.name + "'");
        }
        if (findPalette(decl.name) != nullptr) {
            error(decl.location,
                  "cannot assign to palette '" + decl.name + "'");
        }
        if (findAssetConstant(program_, decl.name))
            error(decl.location, "cannot assign to const '" + decl.name + "'");
        if (current_->name == "draw" && (decl.name == "v" || decl.name == "a")) {
            error(decl.location, "cannot assign to builtin '" + decl.name + "'");
        }
        if (isParam(decl.name)) {
            error(decl.location, "cannot assign to parameter '" + decl.name + "'");
        }
        if (decl.name == "seed") {
            error(decl.location, "'seed' is a reserved name");
        }
        std::optional<ValueType> context = decl.declaredType;
        if (const auto found = typer_.locals().find(decl.name); found != typer_.locals().end()) context = found->second;
        const ValueType type = typer_.typeOf(*decl.value, context);
        if (decl.declaredType && type != valueTypeOf(*decl.declaredType)) {
            const auto* declared = typeName(valueTypeOf(*decl.declaredType));
            SourceError diagnostic{locationOf(*decl.value), std::string(declared) + " '" + decl.name +
                "' needs a " + declared + ", got " + typeName(type), {},
                DiagnosticCode::TypeError, DiagnosticStage::Typing};
            diagnostic.expectedType = declared;
            diagnostic.actualType = typeName(type);
            throw diagnostic;
        }
        if (!isStorable(type)) {
            error(decl.location, "cannot store " + std::string(typeName(type)) +
                                     " in local '" + decl.name +
                                     "' (only scalars can be stored)");
        }
        const auto& locals = typer_.locals();
        const auto existing = locals.find(decl.name);
        if (existing == locals.end()) {
            typer_.declareLocal(decl.name, type);
        } else if (existing->second != type) {
            SourceError diagnostic{decl.location, "local '" + decl.name + "' is " +
                                     typeName(existing->second) +
                                     ", cannot assign " + typeName(type), {},
                                     DiagnosticCode::TypeError, DiagnosticStage::Typing};
            diagnostic.expectedType=typeName(existing->second);
            diagnostic.actualType=typeName(type);
            throw diagnostic;
        }
    }

    bool isParam(const std::string& name) const {
        for (const auto& param : current_->params) {
            if (param.name == name) {
                return true;
            }
        }
        return false;
    }

    const InputDecl* findInput(const std::string& name) const {
        for (const auto& input : program_.asset.inputs) {
            if (input.name == name) {
                return &input;
            }
        }
        return nullptr;
    }

    const OutputDecl* findOutput(const std::string& name) const {
        for (const auto& output : program_.asset.outputs) {
            if (output.name == name) {
                return &output;
            }
        }
        return nullptr;
    }

    const PaletteDecl* findPalette(const std::string& name) const {
        for (const auto& palette : program_.asset.palettes) {
            if (palette.name == name) {
                return &palette;
            }
        }
        return nullptr;
    }

    void checkOutputWrite(const OutputDecl& output, const Expr& value) {
        const ValueType expected = outputValueType(output.kind);
        // Allowed sources: a vector literal of matching arity, or a part
        // output read of matching kind.
        const ValueType got = typer_.typeOf(value);
        const SourceLocation at = locationOf(value);
        if (got != expected) {
            SourceError diagnostic{at, "output '" + output.name + "' expects " +
                          typeName(expected) + ", got " + typeName(got), {},
                          DiagnosticCode::TypeError, DiagnosticStage::Typing};
            diagnostic.expectedType=typeName(expected);
            diagnostic.actualType=typeName(got);
            throw diagnostic;
        }
    }
};

} // namespace

// ---- public API -------------------------------------------------------------

PartInfo partInfoOf(const Asset& asset) {
    PartInfo info;
    for (const auto& input : asset.inputs) {
        info.inputs.push_back(PartInput{input.type, input.name, &input});
    }
    for (const auto& output : asset.outputs) {
        info.outputs.push_back(PartOutput{output.kind, output.isList, output.name});
    }
    return info;
}

void checkAsset(const Program& program,
                const std::map<std::string, PartInfo>& parts) {
    const auto diagnostics = checkAssetDiagnostics(program, parts, 1);
    if (!diagnostics.empty()) throw diagnostics.front();
}

std::vector<SourceError> checkAssetDiagnostics(
    const Program& program, const std::map<std::string, PartInfo>& parts,
    std::size_t limit) {
    std::vector<SourceError> diagnostics;
    Checker(program, parts, diagnostics, limit).run();
    return diagnostics;
}

} // namespace iprop::ips
