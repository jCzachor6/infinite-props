#include "ips/Semantics.hpp"

#include <algorithm>
#include <optional>
#include <set>
#include <utility>

#include "ips/Typing.hpp"

namespace iprop::ips {
namespace {

ValueType outputValueType(OutputKind kind) {
    switch (kind) {
    case OutputKind::Point: return ValueType::Point;
    case OutputKind::Rect: return ValueType::Rect;
    case OutputKind::Circle: return ValueType::Circle;
    }
    return ValueType::Point;
}

// ---- the checker -----------------------------------------------------------

class Checker {
public:
    Checker(const Program& program, const std::map<std::string, PartInfo>& parts)
        : program_(program), parts_(parts), typer_(parts) {
        typer_.resolveFunction = [this](const std::string& name,
                                        const SourceLocation& at,
                                        const std::string&) {
            return checkedFunctionSurface(name, at);
        };
    }

    void run() {
        checkImports();
        checkAssetDecls();
        const Function* draw = findFunction("draw");
        if (draw == nullptr) {
            error(program_.asset.location, "missing 'draw(v, a)' function");
        }
        if (draw->params.size() != 2) {
            error(draw->location,
                  "'draw' must take exactly two parameters (variant, archetype)");
        }
        for (const auto& param : draw->params) {
            if (param.type != ParamType::I32) {
                error(param.location, "the parameters of 'draw' must be i32 "
                                      "(variant, archetype)");
            }
        }
        if (draw->returnType.has_value()) {
            error(draw->location, "'draw' cannot return a value");
        }
        for (const auto& function : program_.asset.functions) {
            checkedFunctionSurface(function.name, function.location);
        }
    }

private:
    const Program& program_;
    const std::map<std::string, PartInfo>& parts_;
    Typer typer_;

    std::map<std::string, const Function*> functions_;
    std::map<std::string, std::optional<ValueType>> returnTypes_;
    std::set<std::string> inProgress_;

    // Per-function state.
    const Function* current_ = nullptr;
    std::uint32_t loopDepth_ = 0;

    [[noreturn]] void error(const SourceLocation& at, std::string message) {
        throw SourceError{at, std::move(message)};
    }

    // ---- declarations -------------------------------------------------------

    void checkImports() {
        std::set<std::string> seen;
        for (const auto& import : program_.imports) {
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

    void checkAssetDecls() {
        if (!program_.asset.sizeX.has_value()) {
            error(program_.asset.location, "missing 'size' declaration");
        }

        std::set<std::string> inputNames;
        for (const auto& input : program_.asset.inputs) {
            if (input.name == "seed" || input.name == "at" ||
                input.name == "v" || input.name == "a") {
                error(input.location, "'" + input.name + "' is a reserved name");
            }
            if (!inputNames.insert(input.name).second) {
                error(input.location, "duplicate input '" + input.name + "'");
            }
            checkInputDefault(input);
        }

        std::set<std::string> outputNames;
        for (const auto& output : program_.asset.outputs) {
            if (!outputNames.insert(output.name).second) {
                error(output.location, "duplicate output '" + output.name + "'");
            }
            if (inputNames.count(output.name) != 0) {
                error(output.location, "output '" + output.name +
                                           "' collides with an input of the same name");
            }
        }

        for (const auto& function : program_.asset.functions) {
            if (!functions_.emplace(function.name, &function).second) {
                error(function.location,
                      "duplicate function '" + function.name + "'");
            }
        }

        std::set<std::string> paletteNames;
        for (const auto& palette : program_.asset.palettes) {
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
            checkPaletteDecl(palette);
        }
    }

    void checkInputDefault(const InputDecl& input) {
        const auto* value = std::get_if<Literal>(input.defaultValue.get());
        if (value == nullptr) {
            error(input.location,
                  "input '" + input.name + "' needs a literal default value");
        }
        const bool ok = [&]() {
            switch (input.type) {
            case InputType::Color:
                return value->kind == Literal::Kind::Color;
            case InputType::F32:
                return value->kind == Literal::Kind::Int ||
                       value->kind == Literal::Kind::Float;
            case InputType::U32:
            case InputType::I32:
                return value->kind == Literal::Kind::Int;
            }
            return false;
        }();
        if (!ok) {
            error(value->location,
                  "default value of input '" + input.name +
                      "' does not match its declared type");
        }
    }

    // The ValueType of a literal expression (string literals are not
    // values). Used for the palette element checks.
    static ValueType literalType(const Literal& literal) {
        switch (literal.kind) {
        case Literal::Kind::Int:
        case Literal::Kind::True:
        case Literal::Kind::False:
            return ValueType::I32;
        case Literal::Kind::Float:
            return ValueType::F32;
        case Literal::Kind::Color:
            return ValueType::Color;
        case Literal::Kind::String:
            return ValueType::Void; // rejected by the caller
        }
        return ValueType::Void;
    }

    void checkPaletteDecl(const PaletteDecl& palette) {
        if (palette.name == "seed" || palette.name == "at" ||
            palette.name == "v" || palette.name == "a") {
            error(palette.location, "'" + palette.name + "' is a reserved name");
        }
        if (palette.elements.empty()) {
            error(palette.location,
                  "palette '" + palette.name + "' needs at least one element");
        }
        std::optional<ValueType> elementType;
        for (const auto& element : palette.elements) {
            const auto* literal = std::get_if<Literal>(element.get());
            if (literal == nullptr) {
                error(locationOf(*element),
                      "palette elements must be literals (compile-time constants)");
            }
            const ValueType type = literalType(*literal);
            if (type == ValueType::Void) {
                error(locationOf(*element),
                      "palette '" + palette.name +
                          "' cannot hold string elements");
            }
            if (!elementType.has_value()) {
                elementType = type;
            } else if (*elementType != type) {
                error(locationOf(*element),
                      "palette '" + palette.name +
                          "' must have elements of one type (" +
                          typeName(*elementType) + " and " + typeName(type) +
                          " found)");
            }
        }
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
                                   returnTypes_[name]};
        }
        const Function* function = findFunction(name);
        if (function == nullptr) {
            return std::nullopt;
        }
        if (inProgress_.count(name) != 0) {
            error(at, "recursive call to '" + name + "' is not supported");
        }
        inProgress_.insert(name);
        checkFunction(*function);
        inProgress_.erase(name);
        return FunctionSurface{declaredParamTypes(*function), returnTypes_[name]};
    }

    void checkFunction(const Function& function) {
        // Lazy checking (a call to a not-yet-checked function) happens in
        // the middle of another function's body: save and restore the
        // per-function state.
        const Function* savedCurrent = current_;
        Typer::Scope savedScope = typer_.saveScope();

        current_ = &function;
        typer_.enterFunction(program_, function, function.name == "draw");
        for (const auto& stmt : function.body) {
            checkStmt(stmt);
        }
        returnTypes_[function.name] = function.returnType.has_value()
                                          ? std::optional<ValueType>(valueTypeOf(
                                                *function.returnType))
                                          : std::nullopt;

        current_ = savedCurrent;
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
                checkStmt(s);
            }
            for (const auto& s : ifStmt->elseBranch) {
                checkStmt(s);
            }
        } else if (const auto* whileStmt = std::get_if<WhileStmt>(&stmt)) {
            checkCondition(*whileStmt->condition, whileStmt->location, "while");
            ++loopDepth_;
            for (const auto& s : whileStmt->body) {
                checkStmt(s);
            }
            --loopDepth_;
        } else if (const auto* forStmt = std::get_if<ForStmt>(&stmt)) {
            checkFor(*forStmt);
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
        if (forStmt.variable == "seed") {
            error(forStmt.location, "'seed' is a reserved name");
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
            checkStmt(s);
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
        const ValueType type = typer_.typeOf(*ret.value);
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
        if (isParam(decl.name)) {
            error(decl.location, "cannot assign to parameter '" + decl.name + "'");
        }
        if (decl.name == "seed") {
            error(decl.location, "'seed' is a reserved name");
        }
        const ValueType type = typer_.typeOf(*decl.value);
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
            error(decl.location, "local '" + decl.name + "' is " +
                                     typeName(existing->second) +
                                     ", cannot assign " + typeName(type));
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
        ValueType got;
        SourceLocation at;
        if (const auto* vector = std::get_if<VectorLit>(&value)) {
            got = typer_.typeOf(value);
            at = vector->location;
        } else {
            got = typer_.typeOf(value);
            at = locationOf(value);
        }
        if (got != expected) {
            error(at, "output '" + output.name + "' expects " +
                          typeName(expected) + ", got " + typeName(got));
        }
    }
};

} // namespace

// ---- public API -------------------------------------------------------------

PartInfo partInfoOf(const Asset& asset) {
    PartInfo info;
    for (const auto& input : asset.inputs) {
        info.inputs.push_back(PartInput{input.type, input.name});
    }
    for (const auto& output : asset.outputs) {
        info.outputs.push_back(PartOutput{output.kind, output.isList, output.name});
    }
    return info;
}

void checkAsset(const Program& program,
                const std::map<std::string, PartInfo>& parts) {
    Checker(program, parts).run();
}

} // namespace iprop::ips
