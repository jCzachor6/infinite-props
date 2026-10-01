#include "ips/Typing.hpp"

#include <set>
#include <utility>

namespace iprop::ips {
namespace {

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
    case OutputKind::Point: return ValueType::Vec2;
    case OutputKind::Rect: return ValueType::Rect;
    case OutputKind::Circle: return ValueType::Circle;
    }
    return ValueType::Vec2;
}

} // namespace

const std::map<std::string, BuiltinSignature>& builtinSignatures() {
    using V = ValueType;
    static const std::map<std::string, BuiltinSignature> table = {
        {"unit", {{}, V::F32}},
        {"stream_push", {{V::I32}, V::Void}},
        {"stream_pop", {{}, V::Void}},
        {"range", {{V::F32, V::F32}, V::F32}},
        {"index", {{V::I32}, V::I32}},
        {"hash", {{V::I32}, V::I32}},
        {"noise", {{V::F32, V::F32, V::I32}, V::F32}},
        {"fault", {{V::F32, V::F32, V::I32}, V::F32}},
        {"smooth", {{V::F32}, V::F32}},
        {"sqrt", {{V::F32}, V::F32}},
        {"sin", {{V::F32}, V::F32}},
        {"cos", {{V::F32}, V::F32}},
        {"abs", {{V::F32}, V::F32}},
        {"floor", {{V::F32}, V::F32}},
        {"ceil", {{V::F32}, V::F32}},
        {"min", {{V::F32, V::F32}, V::F32}},
        {"max", {{V::F32, V::F32}, V::F32}},
        {"pow", {{V::F32, V::F32}, V::F32}},
        {"exp", {{V::F32}, V::F32}},
        {"atan2", {{V::F32, V::F32}, V::F32}},
        {"dist.circle", {{V::Vec2, V::Vec2, V::F32}, V::F32}},
        {"dist.rect", {{V::Vec2, V::Vec2, V::Vec2}, V::F32}},
        {"dist.roundrect", {{V::Vec2, V::Vec2, V::Vec2, V::F32}, V::F32}},
        {"dist.line", {{V::Vec2, V::Vec2, V::Vec2}, V::F32}},
        {"dist.bezier", {{V::Vec2, V::Vec2, V::Vec2, V::Vec2}, V::F32,
                         {{V::Vec2, V::Vec2, V::Vec2, V::Vec2, V::Vec2}}}},
        {"fill", {{V::F32, V::F32}, V::F32}},
        {"gain", {{V::F32, V::F32}, V::F32}},
        {"quintic", {{V::F32}, V::F32}},
        {"noise.grad", {{V::F32, V::F32, V::I32}, V::F32}},
        {"noise.fbm", {{V::F32, V::F32, V::I32, V::I32}, V::F32}},
        {"noise.cell", {{V::F32, V::F32, V::I32}, V::F32}},
        {"warp", {{V::F32, V::F32, V::F32, V::I32}, V::F32}},
        {"cell.jitter", {{V::I32, V::I32, V::I32}, V::Vec2}},
        {"noise.ridge", {{V::F32, V::F32, V::I32, V::I32}, V::F32}},
        {"color.set.a", {{V::Color, V::I32}, V::Color}},
        {"color.mul", {{V::Color, V::Color}, V::Color}},
        {"color.scale", {{V::Color, V::F32}, V::Color}},
        {"color.hsv", {{V::Color, V::F32, V::F32, V::F32}, V::Color}},
        {"dither", {{V::F32, V::I32, V::I32}, V::F32}},
        {"fold", {{V::Vec2, V::I32}, V::Vec2}},
        {"polar", {{V::Vec2}, V::Vec2}},
        {"smoothstep", {{V::F32, V::F32, V::F32}, V::F32}},
        {"remap", {{V::F32, V::F32, V::F32, V::F32, V::F32}, V::F32}},
        {"clamp", {{V::F32, V::F32, V::F32}, V::F32}},
        {"i32trunc", {{V::F32}, V::I32}},
        {"round", {{V::F32}, V::I32}},
        {"lerp", {{V::F32, V::F32, V::F32}, V::F32}},
        {"mix", {{V::Color, V::Color, V::F32}, V::Color}},
        {"shade", {{V::Color, V::I32}, V::Color}},
        {"pixel", {{V::I32, V::I32, V::Color}, V::Void}},
        {"rect", {{V::F32, V::F32, V::F32, V::F32, V::Color}, V::Void}},
        {"line", {{V::F32, V::F32, V::F32, V::F32, V::I32, V::Color}, V::Void}},
        {"curve", {{V::Vec2, V::Vec2, V::Vec2, V::F32, V::Color}, V::Void,
                   {{V::Vec2, V::Vec2, V::Vec2, V::Vec2, V::F32, V::Color}}}},
        {"oval", {{V::F32, V::F32, V::F32, V::F32, V::Color}, V::Void}},
        {"weather", {{V::I32, V::I32}, V::Void}},
        // poly takes a colour plus 2n vertex floats (n >= 3); the arity is
        // validated in typeBuiltinCall.
        {"poly", {{}, V::Void}},
    };
    return table;
}

std::vector<ValueType> declaredParamTypes(const Function& function) {
    std::vector<ValueType> params;
    for (const auto& param : function.params) {
        params.push_back(valueTypeOf(param.type));
    }
    return params;
}

// ---- Typer -------------------------------------------------------------------

Typer::Typer(const std::map<std::string, PartInfo>& parts) : parts_(parts) {}

[[noreturn]] void Typer::error(const SourceLocation& at,
                               std::string message) const {
    throw SourceError{at, std::move(message)};
}

void Typer::enterFunction(const Program& owner, const Function& function,
                          bool isDraw, std::string ns) {
    owner_ = &owner;
    isDraw_ = isDraw;
    ns_ = std::move(ns);
    locals_.clear();
    for (const auto& param : function.params) {
        locals_.emplace(param.name, valueTypeOf(param.type));
    }
}

void Typer::declareLocal(const std::string& name, ValueType type) {
    locals_.emplace(name, type);
}

Typer::Scope Typer::saveScope() const {
    return Scope{owner_, isDraw_, ns_, locals_};
}

void Typer::restoreScope(Scope scope) {
    owner_ = scope.owner;
    isDraw_ = scope.isDraw;
    ns_ = std::move(scope.ns);
    locals_ = std::move(scope.locals);
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

Typer::PartRead Typer::partReadInfo(const Expr& expr) const {
    PartRead info;
    const Path path = flatten(expr);
    if (path.base.empty()) {
        error(path.location, "unsupported member access");
    }
    const auto part = parts_.find(path.base);
    if (part == parts_.end()) {
        error(path.location, "'" + path.base + "' is not an imported part");
    }
    if (path.segments.size() != 2 || path.segments[0] != "outputs") {
        error(path.location, "expected '" + path.base +
                                 ".outputs.<name>' to read a part output");
    }
    info.alias = path.base;
    for (const auto& output : part->second.outputs) {
        if (output.name == path.segments[1]) {
            info.output = &output;
            info.kind = output.kind;
            return info;
        }
    }
    error(path.location, "part '" + path.base + "' has no output '" +
                             path.segments[1] + "'");
}

// ---- expressions -------------------------------------------------------------

ValueType Typer::typeOf(const Expr& expr) {
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
        return typePartRead(expr);
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
    const auto local = locals_.find(id.name);
    if (local != locals_.end()) {
        return local->second;
    }
    // Uniforms are parameters of `draw` only.
    if (isDraw_) {
        for (const auto& input : owner_->asset.inputs) {
            if (input.name == id.name) {
                return inputValueType(input.type);
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
    error(id.location, "undefined name '" + id.name + "'");
}

ValueType Typer::typePartRead(const Expr& expr) {
    const PartRead read = partReadInfo(expr);
    if (read.output->isList) {
        error(locationOf(expr),
              "cannot read list output '" + read.output->name + "'");
    }
    return outputValueType(read.kind);
}

ValueType Typer::typeCall(const Call& call) {
    if (const auto* name = std::get_if<Identifier>(call.callee.get())) {
        if (name->name == "ramp") {
            return typeRamp(*name, call);
        }
        const auto builtin = builtinSignatures().find(name->name);
        if (builtin != builtinSignatures().end()) {
            return typeBuiltinCall(*name, call, builtin->second);
        }
        return typeUserCall(*name, call);
    }
    if (std::holds_alternative<Member>(*call.callee)) {
        // The dotted builtin names (dist.circle, noise.fbm, ...): the
        // bases dist/noise/cell are the reserved namespaces; anything
        // else is a part call.
        const Path path = flatten(*call.callee);
        if (!path.segments.empty() &&
            (path.base == "dist" || path.base == "noise" ||
             path.base == "cell" || path.base == "color")) {
            std::string full = path.base;
            for (const std::string& segment : path.segments) {
                full += "." + segment;
            }
            const auto builtin = builtinSignatures().find(full);
            if (builtin != builtinSignatures().end()) {
                Identifier synthetic{full, path.location};
                return typeBuiltinCall(synthetic, call, builtin->second);
            }
        }
        return typePartCall(call);
    }
    error(call.location, "cannot call this expression");
}

ValueType Typer::typeBuiltinCall(const Identifier& name, const Call& call,
                                 const BuiltinSignature& signature) {
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
            const ValueType expected =
                i == 0 ? ValueType::Color : ValueType::F32;
            const ValueType got = typeOf(*arg.value);
            if (!argFits(expected, got)) {
                error(locationOf(*arg.value),
                      "argument " + std::to_string(i + 1) +
                          " of poly must be " + typeName(expected) +
                          ", got " + typeName(got));
            }
        }
        return ValueType::Void;
    }

    const std::vector<ValueType>* params = &signature.params;
    if (params->size() != call.args.size()) {
        for (const auto& alternate : signature.alternateParams) {
            if (alternate.size() == call.args.size()) {
                params = &alternate;
                break;
            }
        }
    }
    if (params->size() != call.args.size()) {
        std::string arities = std::to_string(signature.params.size());
        for (const auto& alternate : signature.alternateParams) {
            arities += " or " + std::to_string(alternate.size());
        }
        error(call.location,
              "function '" + name.name + "' expects " + arities +
                  " arguments, got " + std::to_string(call.args.size()));
    }
    for (std::size_t i = 0; i < call.args.size(); ++i) {
        const Arg& arg = call.args[i];
        if (arg.name.has_value()) {
            error(arg.location,
                  "function '" + name.name + "' does not take named arguments");
        }
        const ValueType got = typeOf(*arg.value);
        if (!argFits((*params)[i], got)) {
            error(locationOf(*arg.value),
                  "argument " + std::to_string(i + 1) + " of '" + name.name +
                      "' must be " + typeName((*params)[i]) +
                      ", got " + typeName(got));
        }
    }
    return signature.result;
}

ValueType Typer::typeUserCall(const Identifier& name, const Call& call) {
    const std::optional<FunctionSurface> surface =
        resolveFunction(name.name, call.location, ns_);
    if (!surface.has_value()) {
        error(call.location, "undefined function '" + name.name + "'");
    }
    if (surface->params.size() != call.args.size()) {
        error(call.location,
              "function '" + name.name + "' expects " +
                  std::to_string(surface->params.size()) +
                  " arguments, got " + std::to_string(call.args.size()));
    }
    for (std::size_t i = 0; i < call.args.size(); ++i) {
        const Arg& arg = call.args[i];
        if (arg.name.has_value()) {
            error(arg.location,
                  "function '" + name.name + "' does not take named arguments");
        }
        const ValueType got = typeOf(*arg.value);
        if (!argFits(surface->params[i], got)) {
            error(locationOf(*arg.value),
                  "argument " + std::to_string(i + 1) + " of '" + name.name +
                      "' must be " + typeName(surface->params[i]) +
                      ", got " + typeName(got));
        }
    }
    return surface->result.value_or(ValueType::Void);
}

// `part.draw(seed, variant, archetype, at = {x, y}, uniforms...)`
ValueType Typer::typePartCall(const Call& call) {
    const Path path = flatten(*call.callee);
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
    std::set<std::string> transforms;
    for (const auto& arg : call.args) {
        if (!arg.name.has_value()) {
            if (positional >= 3) {
                error(arg.location,
                      "part call takes exactly three positional "
                      "arguments (seed, variant, archetype)");
            }
            const ValueType got = typeOf(*arg.value);
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
                const ValueType got = typeOf(*arg.value);
                if (got != ValueType::Vec2) {
                    error(locationOf(*arg.value),
                          "'at' must be a vec2 {x, y}, got " +
                              std::string(typeName(got)));
                }
                continue;
            }
            if (*arg.name == "mirror_x" || *arg.name == "mirror_y" ||
                *arg.name == "rot") {
                if (!transforms.insert(*arg.name).second) {
                    error(arg.location, "duplicate '" + *arg.name + "' argument");
                }
                const ValueType got = typeOf(*arg.value);
                if (got != ValueType::I32) {
                    error(locationOf(*arg.value),
                          "'" + *arg.name + "' must be an integer, got " +
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
                error(arg.location,
                      "part '" + path.base + "' has no input '" + *arg.name + "'");
            }
            const ValueType got = typeOf(*arg.value);
            if (!argFits(inputValueType(input->type), got)) {
                error(locationOf(*arg.value),
                      "input '" + *arg.name + "' of part '" + path.base +
                          "' must be " + typeName(inputValueType(input->type)) +
                          ", got " + typeName(got));
            }
        }
    }
    if (positional != 3) {
        error(call.location,
              "part call takes exactly three positional "
              "arguments (seed, variant, archetype), got " +
                  std::to_string(positional));
    }
    if (!hasAt) {
        error(call.location,
              "part call requires an 'at = {x, y}' placement argument");
    }
    return ValueType::Void;
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

// `palette[i]` — the index must be i32; the result is the (uniform) element
// type. The checker guarantees literal, homogeneous elements.
ValueType Typer::typeIndex(const Index& index) {
    const auto* name = std::get_if<Identifier>(index.object.get());
    if (name == nullptr) {
        error(locationOf(*index.object), "only palettes can be indexed");
    }
    const PaletteDecl* palette = nullptr;
    for (const auto& candidate : owner_->asset.palettes) {
        if (candidate.name == name->name) {
            palette = &candidate;
            break;
        }
    }
    if (palette == nullptr) {
        error(name->location, "'" + name->name + "' is not a palette");
    }
    const ValueType indexType = typeOf(*index.index);
    if (indexType != ValueType::I32) {
        error(locationOf(*index.index),
              "palette index must be i32, got " + std::string(typeName(indexType)));
    }
    // All elements share one type (checked by the semantic checker); the
    // first element determines it.
    const auto& first = std::get<Literal>(*palette->elements[0]);
    switch (first.kind) {
    case Literal::Kind::Int:
    case Literal::Kind::True:
    case Literal::Kind::False:
        return ValueType::I32;
    case Literal::Kind::Float:
        return ValueType::F32;
    case Literal::Kind::Color:
        return ValueType::Color;
    case Literal::Kind::String:
        break;
    }
    error(name->location, "palette '" + name->name + "' has no valid elements");
}

// `ramp(palette, t)` — the first argument names a palette; the t is the
// numeric position clamped to [0, 1]. The result is the element type (the
// colour or f32 — the i32 palettes are rejected: the interpolation would
// need the widening the caller can do themselves).
ValueType Typer::typeRamp(const Identifier& name, const Call& call) {
    if (call.args.size() != 2) {
        error(call.location, "ramp takes a palette name and a t value");
    }
    for (const Arg& arg : call.args) {
        if (arg.name.has_value()) {
            error(arg.location,
                  "function '" + name.name + "' does not take named arguments");
        }
    }
    const auto* paletteName =
        std::get_if<Identifier>(call.args[0].value.get());
    if (paletteName == nullptr) {
        error(locationOf(*call.args[0].value),
              "ramp's first argument must be a palette name");
    }
    const PaletteDecl* palette = nullptr;
    for (const auto& candidate : owner_->asset.palettes) {
        if (candidate.name == paletteName->name) {
            palette = &candidate;
            break;
        }
    }
    if (palette == nullptr) {
        error(paletteName->location,
              "'" + paletteName->name + "' is not a palette");
    }
    const ValueType t = typeOf(*call.args[1].value);
    if (!isNumeric(t)) {
        error(locationOf(*call.args[1].value),
              "ramp's t must be numeric, got " + std::string(typeName(t)));
    }
    const auto& first = std::get<Literal>(*palette->elements[0]);
    switch (first.kind) {
    case Literal::Kind::Float:
        return ValueType::F32;
    case Literal::Kind::Color:
        return ValueType::Color;
    case Literal::Kind::Int:
    case Literal::Kind::True:
    case Literal::Kind::False:
        error(paletteName->location,
              "ramp requires a color or f32 palette; '" + paletteName->name +
                  "' is i32 (use the explicit lerp instead)");
    case Literal::Kind::String:
        break;
    }
    error(paletteName->location,
          "palette '" + paletteName->name + "' has no valid elements");
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
