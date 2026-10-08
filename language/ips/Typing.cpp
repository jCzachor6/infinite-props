#include "ips/Typing.hpp"
#include "ips/BuiltinArguments.hpp"
#include "ips/Suggestions.hpp"

#include <utility>

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

} // namespace

std::vector<ValueType> declaredParamTypes(const Function& function) {
    std::vector<ValueType> params;
    for (const auto& param : function.params) {
        params.push_back(valueTypeOf(param.type));
    }
    return params;
}

std::vector<std::string> declaredParamNames(const Function& function) {
    std::vector<std::string> names;
    for (const auto& param : function.params) names.push_back(param.name);
    return names;
}

// ---- Typer -------------------------------------------------------------------

Typer::Typer(const std::map<std::string, PartInfo>& parts) : parts_(parts) {}

BoundCallPlan Typer::bind(const Call& call, CallSignature signature, std::string_view name) {
    auto bound = bindCall(call, std::move(signature), name);
    calls_.insert_or_assign(&call, bound);
    return bound;
}

[[noreturn]] void Typer::error(const SourceLocation& at,
                               std::string message) const {
    throw SourceError{at, std::move(message), {}, DiagnosticCode::TypeError, DiagnosticStage::Typing};
}

void Typer::enterFunction(const Program& owner, const Function& function,
                          bool isDraw, std::string ns) {
    owner_ = &owner;
    isDraw_ = isDraw;
    ns_ = std::move(ns);
    locals_.clear();
    invalidLocals_.clear();
    expressions_.clear();
    calls_.clear();
    for (const auto& param : function.params) {
        if (!locals_.emplace(param.name, valueTypeOf(param.type)).second)
            error(param.location, "duplicate parameter '" + param.name + "'");
    }
    if (isDraw) {
        locals_.emplace("v", ValueType::I32);
        locals_.emplace("a", ValueType::I32);
    }
}

void Typer::declareLocal(const std::string& name, ValueType type) {
    invalidLocals_.erase(name);
    locals_.emplace(name, type);
}

void Typer::declareInvalidLocal(const std::string& name) {
    if (!locals_.contains(name)) invalidLocals_.insert(name);
}

Typer::Scope Typer::saveScope() const {
    return Scope{owner_, isDraw_, ns_, locals_, expressions_, invalidLocals_, calls_};
}

void Typer::restoreScope(Scope scope) {
    owner_ = scope.owner;
    isDraw_ = scope.isDraw;
    ns_ = std::move(scope.ns);
    locals_ = std::move(scope.locals);
    expressions_ = std::move(scope.expressions);
    invalidLocals_ = std::move(scope.invalidLocals);
    calls_ = std::move(scope.calls);
}

// ---- member chains ----------------------------------------------------------

Typer::Path Typer::flatten(const Expr& expr) const {
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

const PartInfo* Typer::findPart(const std::string& alias) const {
    const auto found = parts_.find(ns_ + alias);
    return found == parts_.end() ? nullptr : &found->second;
}

std::vector<std::string> Typer::partAliases() const {
    std::vector<std::string> names;
    for (const auto& [path, info] : parts_) {
        (void)info;
        if (path.starts_with(ns_) && path.find('.', ns_.size()) == std::string::npos)
            names.push_back(path.substr(ns_.size()));
    }
    return names;
}

Typer::PartRead Typer::partReadInfo(const Expr& expr) const {
    PartRead info;
    const Path path = flatten(expr);
    if (path.base.empty()) {
        error(path.location, "unsupported member access");
    }
    const auto* part = findPart(path.base);
    if (part == nullptr) {
        error(path.location, "'" + path.base + "' is not an imported part" + didYouMean(path.base, partAliases()));
    }
    if (path.segments.size() != 2 || path.segments[0] != "outputs") {
        error(path.location, "expected '" + path.base +
                                 ".outputs.<name>' to read a part output");
    }
    info.alias = path.base;
    for (const auto& output : part->outputs) {
        if (output.name == path.segments[1]) {
            info.output = &output;
            info.kind = output.kind;
            return info;
        }
    }
    std::vector<std::string> names;
    for (const auto& output : part->outputs) names.push_back(output.name);
    error(path.location, "part '" + path.base + "' has no output '" +
                             path.segments[1] + "'" + didYouMean(path.segments[1], names));
}

// ---- expressions -------------------------------------------------------------

ValueType Typer::typeOf(const Expr& expr) {
    if (const auto found = expressions_.find(&expr); found != expressions_.end())
        return found->second;
    const auto type = infer(expr);
    expressions_.emplace(&expr, type);
    return type;
}

ValueType Typer::infer(const Expr& expr) {
    if (const auto* lit = std::get_if<Literal>(&expr)) {
        return typeLiteral(*lit);
    }
    if (const auto* id = std::get_if<Identifier>(&expr)) {
        return typeIdentifier(*id);
    }
    if (std::holds_alternative<Member>(expr)) {
        const auto& member = std::get<Member>(expr);
        // Resolve the complete part-output path first, including outputs
        // named x or y; a component access then types that point recursively.
        const Path path = flatten(expr);
        if (path.segments.size() == 2 && path.segments[0] == "outputs") {
            return typePartRead(expr);
        }
        const ValueType object = typeOf(*member.object);
        if (object == ValueType::Vec2) {
            if (member.name == "x" || member.name == "y") {
                return ValueType::F32;
            }
            error(member.location,
                  "vec2 has only .x and .y components, got '" +
                      member.name + "'");
        }
        if (object == ValueType::Color) {
            if (colorChannel(member.name)) return ValueType::I32;
            error(member.location, "color has only .r, .g, .b and .a channels, got '." +
                                       member.name + "'");
        }
        // Any other value has no members; only part paths remain above. A
        // channel or component name says which value type it needs.
        const bool channel = colorChannel(member.name).has_value();
        if (channel || member.name == "x" || member.name == "y") {
            const ValueType needed = channel ? ValueType::Color : ValueType::Vec2;
            SourceError mismatch{member.location, "'." + member.name + "' reads a " +
                std::string(channel ? "color channel" : "vec2 component") + " and needs a " +
                typeName(needed) + ", got " + typeName(object), {}, DiagnosticCode::TypeError,
                DiagnosticStage::Typing};
            mismatch.expectedType = typeName(needed);
            mismatch.actualType = typeName(object);
            throw mismatch;
        }
        error(member.location, std::string(typeName(object)) +
                                   " has no components, got '." + member.name + "'");
    }
    if (const auto* call = std::get_if<Call>(&expr)) {
        return typeCall(*call);
    }
    if (const auto* vector = std::get_if<VectorLit>(&expr)) {
        return typeVector(*vector);
    }
    if (const auto* index = std::get_if<Index>(&expr)) {
        return typeIndex(*index);
    }
    if (const auto* bin = std::get_if<Binary>(&expr)) {
        return typeBinary(*bin);
    }
    if (const auto* un = std::get_if<Unary>(&expr)) {
        return typeUnary(*un);
    }
    if (const auto* range = std::get_if<RandomRange>(&expr)) {
        return typeRandomRange(*range);
    }
    error(locationOf(expr), "unsupported expression");
}

ValueType Typer::typeLiteral(const Literal& lit) {
    switch (lit.kind) {
    case Literal::Kind::Int:
    case Literal::Kind::True:
    case Literal::Kind::False:
        return ValueType::I32;
    case Literal::Kind::Float:
        return ValueType::F32;
    case Literal::Kind::Color:
        return ValueType::Color;
    case Literal::Kind::String:
        error(lit.location,
              "string literals are only valid as import paths");
    }
    error(lit.location, "unknown literal kind");
}

ValueType Typer::typeIdentifier(const Identifier& id) {
    if (invalidLocals_.contains(id.name)) throw UnresolvedType{};
    const auto local = locals_.find(id.name);
    if (local != locals_.end()) {
        return local->second;
    }
    // Uniforms are parameters of `draw` only.
    if (isDraw_) {
        for (const auto& input : owner_->asset.inputs) {
            if (input.name == id.name) {
                return valueTypeOf(input.type);
            }
        }
    }
    for (const auto& input : owner_->asset.inputs) {
        if (input.name == id.name) {
            error(id.location,
                  "input '" + id.name + "' is only visible inside 'draw'");
        }
    }
    if (id.name == "seed") {
        return ValueType::I32;
    }
    // A bare palette name is not a value — it must be indexed.
    for (const auto& palette : owner_->asset.palettes) {
        if (palette.name == id.name) {
            error(id.location, "palette '" + id.name + "' must be indexed ('" +
                                   id.name + "[i]')");
        }
    }
    std::vector<std::string> candidates{"seed"};
    for (const auto& [name, type] : locals_) { (void)type; candidates.push_back(name); }
    if (isDraw_) for (const auto& input : owner_->asset.inputs) candidates.push_back(input.name);
    for (const auto& palette : owner_->asset.palettes) candidates.push_back(palette.name);
    error(id.location, "undefined name '" + id.name + "'" + didYouMean(id.name, candidates));
}

ValueType Typer::typePartRead(const Expr& expr) {
    const PartRead read = partReadInfo(expr);
    if (read.output->isList) {
        error(locationOf(expr),
              "cannot read list output '" + read.output->name + "'");
    }
    return outputValueType(read.kind);
}

ValueType Typer::typeVector(const VectorLit& vector) {
    const auto numericElements = [&](ValueType type) {
        for (const auto& element : vector.elements) {
            const ValueType got = typeOf(*element);
            if (!isNumeric(got)) {
                error(locationOf(*element),
                      "vector elements must be numeric, got " +
                          std::string(typeName(got)));
            }
        }
        return type;
    };
    switch (vector.elements.size()) {
    case 2: return numericElements(ValueType::Vec2);
    case 3: return numericElements(ValueType::Circle);
    case 4: return numericElements(ValueType::Rect);
    default:
        error(vector.location,
              "vector literal must have 2 (vec2), 3 (circle) or 4 (rect) "
              "elements, got " +
                  std::to_string(vector.elements.size()));
    }
}

const ResolvedPalette& Typer::palette(const Identifier& name) {
    const auto& decl = findPalette(*owner_,name);
    if (const auto found=palettes_.find(&decl); found!=palettes_.end()) return found->second;
    return palettes_.emplace(&decl,resolvePalette(decl)).first->second;
}

// `palette[i]` — the index must be i32; the result is the (uniform) element
// type. The checker guarantees literal, homogeneous elements.
ValueType Typer::typeIndex(const Index& index) {
    const auto* name = std::get_if<Identifier>(index.object.get());
    if (name == nullptr) {
        error(locationOf(*index.object), "only palettes can be indexed");
    }
    const auto& resolved = palette(*name);
    const ValueType indexType = typeOf(*index.index);
    if (indexType != ValueType::I32) {
        error(locationOf(*index.index),
              "palette index must be i32, got " + std::string(typeName(indexType)));
    }
    return resolved.type;
}

// `ramp(palette, t)` — the first argument names a palette; the t is the
// numeric position clamped to [0, 1]. The result is the element type (the
// colour or f32 — the i32 palettes are rejected: the interpolation would
// need the widening the caller can do themselves).
ValueType Typer::typeRamp(const Identifier& name, const Call& call) {
    if (call.args.size() != 2) {
        error(call.location, "ramp takes a palette name and a t value");
    }
    const auto bound = bind(call, rampCallSignature(), name.name);
    const auto& args = bound.arguments;
    const auto* paletteName =
        std::get_if<Identifier>(args[0]->value.get());
    if (paletteName == nullptr) {
        error(locationOf(*args[0]->value),
              "ramp's first argument must be a palette name");
    }
    const auto& resolved = palette(*paletteName);
    const ValueType t = typeOf(*args[1]->value);
    if (!isNumeric(t)) {
        error(locationOf(*args[1]->value),
              "ramp's t must be numeric, got " + std::string(typeName(t)));
    }
    if (resolved.type == ValueType::I32) {
        error(paletteName->location,
              "ramp requires a color or f32 palette; '" + paletteName->name +
                  "' is i32 (use the explicit lerp instead)");
    }
    return resolved.type;
}

// `lo to hi [with d]`: i32 bounds draw an i32; any f32 bound widens both to
// f32. Two colors or two vec2 values draw one position between them. Bounds
// type before the distribution, in evaluation order.
ValueType Typer::typeRandomRange(const RandomRange& range) {
    const ValueType lower = typeOf(*range.lower);
    const ValueType upper = typeOf(*range.upper);
    const bool ends = lower == upper && (lower == ValueType::Color || lower == ValueType::Vec2);
    if (!ends && (!isNumeric(lower) || !isNumeric(upper))) {
        error(range.location, "'to' needs two numbers, two colors or two vec2 values, got " +
                                  std::string(typeName(lower)) + " and " + typeName(upper));
    }
    if (range.distribution) {
        const ValueType shape = typeOf(*range.distribution);
        if (shape != ValueType::Distribution) {
            SourceError mismatch{locationOf(*range.distribution), "'with' needs a distribution, got " +
                std::string(typeName(shape)), {}, DiagnosticCode::TypeError, DiagnosticStage::Typing};
            mismatch.expectedType = typeName(ValueType::Distribution);
            mismatch.actualType = typeName(shape);
            throw mismatch;
        }
    }
    return ends ? lower : promote(lower, upper);
}

ValueType Typer::typeBinary(const Binary& bin) {
    const ValueType left = typeOf(*bin.left);
    const ValueType right = typeOf(*bin.right);
    switch (bin.op) {
    case Binary::Op::Add:
    case Binary::Op::Sub:
    case Binary::Op::Mul:
    case Binary::Op::Div:
        if (left == ValueType::Vec2 || right == ValueType::Vec2) {
            const bool vectors = left == ValueType::Vec2 && right == ValueType::Vec2;
            const bool vectorScalar = left == ValueType::Vec2 && isNumeric(right) &&
                                      (bin.op == Binary::Op::Mul || bin.op == Binary::Op::Div);
            const bool scalarVector = isNumeric(left) && right == ValueType::Vec2 &&
                                      bin.op == Binary::Op::Mul;
            if (!vectors && !vectorScalar && !scalarVector) {
                const char* op = bin.op == Binary::Op::Add ? "+"
                                 : bin.op == Binary::Op::Sub ? "-"
                                 : bin.op == Binary::Op::Mul ? "*" : "/";
                error(bin.location,
                      std::string("operator '") + op + "' does not support " +
                          typeName(left) + " and " +
                          typeName(right));
            }
            return ValueType::Vec2;
        }
        if (!isNumeric(left) || !isNumeric(right)) {
            error(bin.location, "arithmetic requires numeric operands, got " +
                                     std::string(typeName(left)) + " and " +
                                     typeName(right));
        }
        return promote(left, right);
    case Binary::Op::Mod:
        // The VM has no f32 remainder: % is integer-only.
        if (left != ValueType::I32 || right != ValueType::I32) {
            error(bin.location, "'%' requires i32 operands, got " +
                                     std::string(typeName(left)) + " and " +
                                     typeName(right));
        }
        return ValueType::I32;
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
    case Binary::Op::Shl:
    case Binary::Op::Shr:
        if (left != ValueType::I32 || right != ValueType::I32) {
            error(bin.location, "integer operation requires i32 operands, got " +
                                     std::string(typeName(left)) + " and " +
                                     typeName(right));
        }
        return ValueType::I32;
    }
    return ValueType::I32;
}

ValueType Typer::typeUnary(const Unary& un) {
    const ValueType operand = typeOf(*un.operand);
    switch (un.op) {
    case Unary::Op::Neg:
        if (!isNumeric(operand) && operand != ValueType::Vec2) {
            error(un.location,
                  "unary minus requires a numeric or vec2 operand, got " +
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

} // namespace iprop::ips
