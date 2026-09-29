#include "ips/Semantics.hpp"

#include <algorithm>
#include <optional>
#include <set>
#include <utility>

#include "ips/Parser.hpp"
#include "ips/Types.hpp"

namespace iprop::ips {
namespace {

// ---- builtins (VM primitives, spec §9) -----------------------------------

struct Builtin {
    std::vector<ValueType> params;
    ValueType result = ValueType::Void;
};

const std::map<std::string, Builtin>& builtins() {
    using V = ValueType;
    static const std::map<std::string, Builtin> table = {
        {"unit", {{}, V::F32}},
        {"range", {{V::F32, V::F32}, V::F32}},
        {"index", {{V::I32}, V::I32}},
        {"hash", {{V::I32}, V::I32}},
        {"noise", {{V::F32, V::F32, V::I32}, V::F32}},
        {"fault", {{V::F32, V::F32, V::I32}, V::F32}},
        {"smooth", {{V::F32}, V::F32}},
        {"lerp", {{V::F32, V::F32, V::F32}, V::F32}},
        {"mix", {{V::Color, V::Color, V::F32}, V::Color}},
        {"shade", {{V::Color, V::I32}, V::Color}},
        {"pixel", {{V::I32, V::I32, V::Color}, V::Void}},
        {"rect", {{V::F32, V::F32, V::F32, V::F32, V::Color}, V::Void}},
        {"line", {{V::F32, V::F32, V::F32, V::F32, V::I32, V::Color}, V::Void}},
        {"oval", {{V::F32, V::F32, V::F32, V::F32, V::Color}, V::Void}},
        {"weather", {{V::I32, V::I32}, V::Void}},
        // poly takes a colour plus 2n vertex floats (n >= 3); the arity is
        // validated in checkBuiltinArgs.
        {"poly", {{}, V::Void}},
    };
    return table;
}

ValueType inputValueType(InputType type) {
    switch (type) {
    case InputType::Color: return ValueType::Color;
    case InputType::F32: return ValueType::F32;
    case InputType::U32:
    case InputType::I32: return ValueType::I32;
    }
    return ValueType::I32;
}

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
        : program_(program), parts_(parts) {}

    void run() {
        checkImports();
        checkAssetDecls();
        const Function* draw = findFunction("draw");
        if (draw == nullptr) {
            error(program_.asset.location,
                  "missing 'draw(v, a)' function");
        }
        if (draw->params.size() != 2) {
            error(draw->location, "'draw' must take exactly two parameters (variant, archetype)");
        }
        for (const auto& function : program_.asset.functions) {
            checkedReturnType(function.name, function.location);
        }
    }

private:
    const Program& program_;
    const std::map<std::string, PartInfo>& parts_;

    std::map<std::string, const Function*> functions_;
    std::map<std::string, std::optional<ValueType>> returnTypes_;
    std::set<std::string> inProgress_;

    // Per-function state.
    const Function* current_ = nullptr;
    std::map<std::string, ValueType> locals_;
    std::optional<ValueType> pendingReturn_;

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
    }

    void checkInputDefault(const InputDecl& input) {
        const auto* value = std::get_if<Literal>(input.defaultValue.get());
        if (value == nullptr) {
            error(input.location, "input '" + input.name +
                                       "' needs a literal default value");
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
                  "default value of input '" + input.name + "' does not match its declared type");
        }
    }

    // ---- functions ----------------------------------------------------------

    const Function* findFunction(const std::string& name) const {
        const auto found = functions_.find(name);
        return found == functions_.end() ? nullptr : found->second;
    }

    // Returns the checked return type of `name`, checking the body lazily on
    // first use. Recursive calls are rejected.
    std::optional<ValueType> checkedReturnType(const std::string& name,
                                               const SourceLocation& at) {
        if (returnTypes_.count(name) != 0) {
            return returnTypes_[name];
        }
        const Function* function = findFunction(name);
        if (function == nullptr) {
            error(at, "undefined function '" + name + "'");
        }
        if (inProgress_.count(name) != 0) {
            error(at, "recursive call to '" + name + "' is not supported");
        }
        inProgress_.insert(name);
        checkFunction(*function);
        inProgress_.erase(name);
        return returnTypes_[name];
    }

    void checkFunction(const Function& function) {
        // Lazy checking (a call to a not-yet-checked function) happens in
        // the middle of another function's body: save and restore the
        // per-function state.
        const Function* savedCurrent = current_;
        auto savedLocals = std::move(locals_);
        const std::optional<ValueType> savedPending = pendingReturn_;
        locals_.clear();
        pendingReturn_ = std::nullopt;

        current_ = &function;
        for (const auto& param : function.params) {
            locals_.emplace(param, ValueType::I32);
        }
        for (const auto& stmt : function.body) {
            checkStmt(stmt);
        }
        returnTypes_[function.name] = pendingReturn_;

        current_ = savedCurrent;
        locals_ = std::move(savedLocals);
        pendingReturn_ = savedPending;
    }

    // ---- statements ---------------------------------------------------------

    void checkStmt(const Stmt& stmt) {
        if (const auto* decl = std::get_if<VarDecl>(&stmt)) {
            checkVarDecl(*decl);
        } else if (const auto* exprStmt = std::get_if<ExprStmt>(&stmt)) {
            (void)checkExpr(*exprStmt->expr);
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
            for (const auto& s : whileStmt->body) {
                checkStmt(s);
            }
        } else if (const auto* ret = std::get_if<ReturnStmt>(&stmt)) {
            if (ret->value) {
                const ValueType type = checkExpr(*ret->value);
                if (!isStorable(type)) {
                    error(ret->location,
                          "cannot return " + std::string(typeName(type)) +
                              " (only scalars can be returned)");
                }
                if (pendingReturn_.has_value() && *pendingReturn_ != type) {
                    error(ret->location,
                          "inconsistent return type: expected " +
                              std::string(typeName(*pendingReturn_)) + ", got " +
                              std::string(typeName(type)));
                }
                pendingReturn_ = type;
            }
        }
    }

    void checkCondition(const Expr& condition, const SourceLocation& at,
                        const char* what) {
        const ValueType type = checkExpr(condition);
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
        if (isParam(decl.name)) {
            error(decl.location, "cannot assign to parameter '" + decl.name + "'");
        }
        if (decl.name == "seed") {
            error(decl.location, "'seed' is a reserved name");
        }
        const ValueType type = checkExpr(*decl.value);
        if (!isStorable(type)) {
            error(decl.location, "cannot store " + std::string(typeName(type)) +
                                     " in local '" + decl.name +
                                     "' (only scalars can be stored)");
        }
        const auto existing = locals_.find(decl.name);
        if (existing == locals_.end()) {
            locals_.emplace(decl.name, type);
        } else if (existing->second != type) {
            error(decl.location, "local '" + decl.name + "' is " +
                                     typeName(existing->second) +
                                     ", cannot assign " + typeName(type));
        }
    }

    bool isParam(const std::string& name) const {
        const auto& params = current_->params;
        return std::find(params.begin(), params.end(), name) != params.end();
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

    void checkOutputWrite(const OutputDecl& output, const Expr& value) {
        const ValueType expected = outputValueType(output.kind);
        // Allowed sources: a vector literal of matching arity, or a part
        // output read of matching kind.
        if (const auto* vector = std::get_if<VectorLit>(&value)) {
            const ValueType got = checkVector(*vector);
            if (got != expected) {
                error(vector->location, "output '" + output.name + "' expects " +
                                            typeName(expected) + ", got " +
                                            typeName(got));
            }
            return;
        }
        const ValueType got = checkExpr(value);
        if (got != expected) {
            error(locationOf(value), "output '" + output.name + "' expects " +
                                         typeName(expected) + ", got " +
                                         typeName(got));
        }
    }

    // ---- expressions --------------------------------------------------------

    ValueType checkExpr(const Expr& expr) {
        if (const auto* lit = std::get_if<Literal>(&expr)) {
            switch (lit->kind) {
            case Literal::Kind::Int:
            case Literal::Kind::True:
            case Literal::Kind::False:
                return ValueType::I32;
            case Literal::Kind::Float:
                return ValueType::F32;
            case Literal::Kind::Color:
                return ValueType::Color;
            case Literal::Kind::String:
                error(lit->location,
                      "string literals are only valid as import paths");
            }
        }
        if (const auto* id = std::get_if<Identifier>(&expr)) {
            return checkIdentifier(*id);
        }
        if (std::holds_alternative<Member>(expr)) {
            return checkPartRead(expr);
        }
        if (const auto* call = std::get_if<Call>(&expr)) {
            return checkCall(*call);
        }
        if (const auto* vector = std::get_if<VectorLit>(&expr)) {
            return checkVector(*vector);
        }
        if (const auto* bin = std::get_if<Binary>(&expr)) {
            return checkBinary(*bin);
        }
        if (const auto* un = std::get_if<Unary>(&expr)) {
            return checkUnary(*un);
        }
        error(locationOf(expr), "unsupported expression");
    }

    ValueType checkIdentifier(const Identifier& id) {
        const auto local = locals_.find(id.name);
        if (local != locals_.end()) {
            return local->second;
        }
        if (const InputDecl* input = findInput(id.name)) {
            return inputValueType(input->type);
        }
        if (id.name == "seed") {
            return ValueType::I32;
        }
        error(id.location, "undefined name '" + id.name + "'");
    }

    // A member chain that is not a call: the only legal form is
    // `part.outputs.name` (reading a part output).
    ValueType checkPartRead(const Expr& expr) {
        Path path = flatten(expr);
        if (path.base.empty()) {
            error(path.location, "unsupported member access");
        }
        const auto part = parts_.find(path.base);
        if (part == parts_.end()) {
            error(path.location,
                  "'" + path.base + "' is not an imported part");
        }
        if (path.segments.size() != 2 || path.segments[0] != "outputs") {
            error(path.location, "expected '" + path.base +
                                     ".outputs.<name>' to read a part output");
        }
        for (const auto& output : part->second.outputs) {
            if (output.name == path.segments[1]) {
                if (output.isList) {
                    error(path.location, "cannot read list output '" +
                                             output.name + "'");
                }
                return outputValueType(output.kind);
            }
        }
        error(path.location, "part '" + path.base + "' has no output '" +
                                 path.segments[1] + "'");
    }

    ValueType checkCall(const Call& call) {
        if (const auto* name = std::get_if<Identifier>(call.callee.get())) {
            return checkNamedCall(*name, call);
        }
        if (std::holds_alternative<Member>(*call.callee)) {
            return checkPartCall(call);
        }
        error(call.location, "cannot call this expression");
    }

    ValueType checkNamedCall(const Identifier& name, const Call& call) {
        const auto builtin = builtins().find(name.name);
        if (builtin != builtins().end()) {
            checkBuiltinArgs(name, call, builtin->second);
            return builtin->second.result;
        }
        // User function.
        const std::optional<ValueType> result =
            checkedReturnType(name.name, call.location);
        const Function* function = findFunction(name.name);
        if (function->params.size() != call.args.size()) {
            error(call.location, "function '" + name.name + "' expects " +
                                     std::to_string(function->params.size()) +
                                     " arguments, got " +
                                     std::to_string(call.args.size()));
        }
        for (const auto& arg : call.args) {
            if (arg.name.has_value()) {
                error(arg.location, "function '" + name.name +
                                        "' does not take named arguments");
            }
            const ValueType got = checkExpr(*arg.value);
            if (!argFits(ValueType::I32, got)) {
                error(locationOf(*arg.value), "argument of function '" + name.name +
                                                  "' must be i32, got " +
                                                  typeName(got));
            }
        }
        return result.has_value() ? *result : ValueType::Void;
    }

    void checkBuiltinArgs(const Identifier& name, const Call& call,
                          const Builtin& builtin) {
        // poly(colour, x0, y0, x1, y1, ...) — colour then 2n floats, n >= 3.
        if (name.name == "poly") {
            if (call.args.size() < 7 || call.args.size() % 2 == 0) {
                error(call.location,
                      "poly expects a colour and at least three (x, y) vertex pairs");
            }
            for (std::size_t i = 0; i < call.args.size(); ++i) {
                const Arg& arg = call.args[i];
                if (arg.name.has_value()) {
                    error(arg.location, "poly does not take named arguments");
                }
                const ValueType expected = i == 0 ? ValueType::Color : ValueType::F32;
                const ValueType got = checkExpr(*arg.value);
                if (!argFits(expected, got)) {
                    error(locationOf(*arg.value), "argument " + std::to_string(i + 1) +
                                                      " of poly must be " + typeName(expected) +
                                                      ", got " + typeName(got));
                }
            }
            return;
        }

        if (builtin.params.size() != call.args.size()) {
            error(call.location, "function '" + name.name + "' expects " +
                                     std::to_string(builtin.params.size()) +
                                     " arguments, got " +
                                     std::to_string(call.args.size()));
        }
        for (std::size_t i = 0; i < call.args.size(); ++i) {
            const Arg& arg = call.args[i];
            if (arg.name.has_value()) {
                error(arg.location, "function '" + name.name +
                                        "' does not take named arguments");
            }
            const ValueType got = checkExpr(*arg.value);
            if (!argFits(builtin.params[i], got)) {
                error(locationOf(*arg.value),
                      "argument " + std::to_string(i + 1) + " of '" + name.name +
                          "' must be " + typeName(builtin.params[i]) +
                          ", got " + typeName(got));
            }
        }
    }

    // `part.draw(seed, variant, archetype, at = {x, y}, uniforms...)`
    ValueType checkPartCall(const Call& call) {
        Path path = flatten(*call.callee);
        if (path.base.empty() || path.segments.size() != 1 ||
            path.segments[0] != "draw") {
            error(path.location, "expected 'part.draw(...)'");
        }
        const auto part = parts_.find(path.base);
        if (part == parts_.end()) {
            error(path.location, "'" + path.base + "' is not an imported part");
        }

        // Exactly three positional arguments: seed, variant, archetype.
        std::size_t positional = 0;
        bool hasAt = false;
        for (const auto& arg : call.args) {
            if (!arg.name.has_value()) {
                if (positional >= 3) {
                    error(arg.location, "part call takes exactly three positional "
                                        "arguments (seed, variant, archetype)");
                }
                const ValueType got = checkExpr(*arg.value);
                if (!argFits(ValueType::I32, got)) {
                    error(locationOf(*arg.value),
                          "part call argument must be an integer, got " +
                              std::string(typeName(got)));
                }
                ++positional;
            } else {
                if (*arg.name == "at") {
                    if (hasAt) {
                        error(arg.location, "duplicate 'at' argument");
                    }
                    hasAt = true;
                    const ValueType got = checkExpr(*arg.value);
                    if (got != ValueType::Point) {
                        error(locationOf(*arg.value),
                              "'at' must be a point {x, y}, got " +
                                  std::string(typeName(got)));
                    }
                    continue;
                }
                // A uniform override: must be a declared input of the part.
                const PartInput* input = nullptr;
                for (const auto& candidate : part->second.inputs) {
                    if (candidate.name == *arg.name) {
                        input = &candidate;
                        break;
                    }
                }
                if (input == nullptr) {
                    error(arg.location, "part '" + path.base + "' has no input '" +
                                            *arg.name + "'");
                }
                const ValueType got = checkExpr(*arg.value);
                if (!argFits(inputValueType(input->type), got)) {
                    error(locationOf(*arg.value),
                          "input '" + *arg.name + "' of part '" + path.base +
                              "' must be " + typeName(inputValueType(input->type)) +
                              ", got " + typeName(got));
                }
            }
        }
        if (positional != 3) {
            error(call.location, "part call takes exactly three positional "
                                  "arguments (seed, variant, archetype), got " +
                                      std::to_string(positional));
        }
        if (!hasAt) {
            error(call.location,
                  "part call requires an 'at = {x, y}' placement argument");
        }
        return ValueType::Void;
    }

    ValueType checkVector(const VectorLit& vector) {
        switch (vector.elements.size()) {
        case 2: return withNumericElements(vector, ValueType::Point);
        case 3: return withNumericElements(vector, ValueType::Circle);
        case 4: return withNumericElements(vector, ValueType::Rect);
        default:
            error(vector.location,
                  "vector literal must have 2 (point), 3 (circle) or 4 (rect) "
                  "elements, got " +
                      std::to_string(vector.elements.size()));
        }
    }

    ValueType withNumericElements(const VectorLit& vector, ValueType type) {
        for (const auto& element : vector.elements) {
            const ValueType got = checkExpr(*element);
            if (!isNumeric(got)) {
                error(locationOf(*element),
                      "vector elements must be numeric, got " +
                          std::string(typeName(got)));
            }
        }
        return type;
    }

    ValueType checkBinary(const Binary& bin) {
        const ValueType left = checkExpr(*bin.left);
        const ValueType right = checkExpr(*bin.right);
        switch (bin.op) {
        case Binary::Op::Add:
        case Binary::Op::Sub:
        case Binary::Op::Mul:
        case Binary::Op::Div:
        case Binary::Op::Mod:
            if (!isNumeric(left) || !isNumeric(right)) {
                error(bin.location, "arithmetic requires numeric operands, got " +
                                         std::string(typeName(left)) + " and " +
                                         typeName(right));
            }
            return promote(left, right);
        case Binary::Op::Eq:
        case Binary::Op::Ne:
        case Binary::Op::Lt:
        case Binary::Op::Le:
        case Binary::Op::Gt:
        case Binary::Op::Ge:
            if (!isNumeric(left) || !isNumeric(right)) {
                error(bin.location, "comparison requires numeric operands, got " +
                                         std::string(typeName(left)) + " and " +
                                         typeName(right));
            }
            return ValueType::I32;
        case Binary::Op::And:
        case Binary::Op::Or:
        case Binary::Op::Xor:
        case Binary::Op::BitAnd:
        case Binary::Op::BitOr:
            if (left != ValueType::I32 || right != ValueType::I32) {
                error(bin.location, "integer operation requires i32 operands, got " +
                                         std::string(typeName(left)) + " and " +
                                         typeName(right));
            }
            return ValueType::I32;
        }
        return ValueType::I32;
    }

    ValueType checkUnary(const Unary& un) {
        const ValueType operand = checkExpr(*un.operand);
        switch (un.op) {
        case Unary::Op::Neg:
            if (!isNumeric(operand)) {
                error(un.location,
                      "unary minus requires a numeric operand, got " +
                          std::string(typeName(operand)));
            }
            return operand;
        case Unary::Op::Not:
        case Unary::Op::BitNot:
            if (operand != ValueType::I32) {
                error(un.location,
                      "logical/bitwise not requires an i32 operand, got " +
                          std::string(typeName(operand)));
            }
            return ValueType::I32;
        }
        return ValueType::I32;
    }

    // ---- member chains ------------------------------------------------------

    struct Path {
        std::string base; // empty if the base is not an identifier
        std::vector<std::string> segments; // outermost first
        SourceLocation location;
    };

    Path flatten(const Expr& expr) const {
        Path path;
        path.location = locationOf(expr);
        std::vector<std::string> reversed;
        const Expr* current = &expr;
        while (const auto* member = std::get_if<Member>(current)) {
            reversed.push_back(member->name);
            current = member->object.get();
        }
        if (const auto* base = std::get_if<Identifier>(current)) {
            path.base = base->name;
        }
        path.segments.assign(reversed.rbegin(), reversed.rend());
        return path;
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
        info.outputs.push_back(
            PartOutput{output.kind, output.isList, output.name});
    }
    return info;
}

void checkAsset(const Program& program,
                const std::map<std::string, PartInfo>& parts) {
    Checker(program, parts).run();
}

std::vector<std::string> resolveImports(
    std::string_view entryPath,
    const std::function<std::string(std::string_view path)>& loadSource) {
    std::vector<std::string> order;
    std::set<std::string> done;
    std::vector<std::string> chain;
    std::set<std::string> inChain;
    SourceLocation chainImportLocation;

    std::function<void(const std::string&)> visit =
        [&](const std::string& path) {
            if (done.count(path) != 0) {
                return;
            }
            if (inChain.count(path) != 0) {
                // Find where the cycle starts and describe it.
                std::string message = "import cycle: ";
                std::size_t start = 0;
                while (chain[start] != path) {
                    ++start;
                }
                for (std::size_t i = start; i < chain.size(); ++i) {
                    message += chain[i] + " -> ";
                }
                message += path;
                throw SourceError{chainImportLocation, std::move(message)};
            }
            inChain.insert(path);
            chain.push_back(path);

            std::string source;
            try {
                source = loadSource(path);
            } catch (const std::exception& e) {
                throw SourceError{chainImportLocation,
                                  std::string("cannot load '") + path +
                                      "': " + e.what()};
            }
            const Program program = parse(source);
            for (const auto& import : program.imports) {
                chainImportLocation = import.location;
                visit(import.path);
            }
            chain.pop_back();
            inChain.erase(path);
            done.insert(path);
            order.push_back(path);
        };

    visit(std::string(entryPath));
    return order;
}

} // namespace iprop::ips
