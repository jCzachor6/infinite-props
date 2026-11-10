#include "ips/Typing.hpp"
#include "ips/BuiltinArguments.hpp"
#include "ips/Suggestions.hpp"
#include "ips/PartTransform.hpp"
#include "ips/AssetConstants.hpp"
#include <algorithm>

namespace iprop::ips {
namespace {
std::vector<std::string> builtinNames() {
    std::vector<std::string> names{"ramp"};
    for (const auto& [name, spec] : builtinSpecs()) { (void)spec; names.push_back(name); }
    return names;
}
}

bool Typer::namesValue(const std::string& name) const {
    if (name == "seed" || locals_.contains(name)) return true;
    const auto named = [&](const auto& declarations) {
        return std::any_of(declarations.begin(), declarations.end(),
                           [&](const auto& declaration) { return declaration.name == name; });
    };
    return named(owner_->asset.constants) || named(owner_->asset.palettes) ||
           (isDraw_ && named(owner_->asset.inputs));
}

// The namespace whose functions take a value of this type first.
std::string Typer::methodNamespace(ValueType type) {
    if (type == ValueType::Vec2) return "vec2";
    if (type == ValueType::Color) return "color";
    if (type == ValueType::Mask) return "mask";
    if (type == ValueType::Layer) return "surface";
    if (type == ValueType::Regions2d) return "regions";
    if (type == ValueType::Diagram2d) return "diagram";
    if (type == ValueType::Part2d) return "part";
    if (type == ValueType::Parts2d) return "parts";
    if (type.isList()) return "list";
    return {};
}

// A helper call recurses through here into the helper's body, so only the
// plain-name dispatch stays in this function.
ValueType Typer::typeCall(const Call& call) {
    if (const auto* name = std::get_if<Identifier>(call.callee.get())) {
        if (name->name == "ramp") {
            return typeRamp(*name, call);
        }
        // A helper cannot share a builtin's name, so the name alone decides.
        const auto builtin = builtinSpecs().find(name->name);
        if (builtin != builtinSpecs().end()) {
            return typeBuiltinCall(*name, call, builtin->second);
        }
        return typeUserCall(*name, call);
    }
    if (std::holds_alternative<Member>(*call.callee)) return typeMemberCall(call);
    error(call.location, "cannot call this expression");
}

ValueType Typer::typeMemberCall(const Call& call) {
    // A dotted name is a builtin when the builtin table has it. An
    // import alias cannot be a builtin namespace, so the first segment
    // tells a namespace member from a part call.
    const Path path = flatten(*call.callee);
    if (!path.segments.empty()) {
        std::string full = path.base;
        for (const std::string& segment : path.segments) {
            full += "." + segment;
        }
        // Recognized static imports precede builtin spelling suggestions
        // (e.g. pot.draw resembles part.draw).
        if (path.segments.size() == 1 && path.segments[0] == "draw" && findPart(path.base))
            return typePartCall(call);
        const auto builtin = builtinSpecs().find(full);
        if (builtin != builtinSpecs().end()) {
            Identifier synthetic{full, path.location};
            return typeBuiltinCall(synthetic, call, builtin->second);
        }
        const auto former = formerBuiltinHint(full);
        const auto hint = former.empty() ? didYouMean(full, builtinNames()) : former;
        if (isBuiltinNamespace(path.base) || (!hint.empty() && !namesValue(path.base)))
            error(path.location, "undefined builtin '" + full + "'" + hint);
    }
    // A call on a value: the language has only the statically resolved
    // methods the parser recognizes, so name the function to use instead.
    const auto& member = std::get<Member>(*call.callee);
    if (!findPart(path.base) && (path.base.empty() || namesValue(path.base))) {
        const auto receiver = typeOf(*member.object);
        const auto space = methodNamespace(receiver);
        std::string hint;
        if (!space.empty() && builtinSpecs().contains(space + "." + member.name))
            hint = "; write " + space + "." + member.name + "(...) with the value as its first argument";
        else if (builtinSpecs().contains(member.name))
            hint = "; write " + member.name + "(...) with the value as an argument";
        error(member.location, std::string(typeName(receiver)) + " has no method '" + member.name + "'" + hint);
    }
    return typePartCall(call);
}

ValueType Typer::typeBuiltinCall(const Identifier& name, const Call& call,
                                 const BuiltinSpec& signature) {
    auto callSignature = builtinCallSignature(signature, call, name.name);
    if (signature.lowering == BuiltinLowering::SurfacePaint) {
        // The source's type selects the layer or the mask overloads.
        const Arg* source = nullptr;
        for (const auto& arg : call.args) if (arg.name && *arg.name == "source") { source = &arg; break; }
        if (!source) for (const auto& arg : call.args) if (!arg.name) { source = &arg; break; }
        if (source) {
            const auto type = typeOf(*source->value);
            if (type != ValueType::Layer && type != ValueType::Mask)
                error(locationOf(*source->value), "surface.paint source must be a layer or a mask, got " + std::string(typeName(type)));
            callSignature = surfacePaintCallSignature(call, type);
        }
    }
    const auto bound = bind(call, std::move(callSignature), name.name);
    const auto& args = bound.arguments;
    if (signature.lowering == BuiltinLowering::SurfacePaint &&
        bound.signature.parameters.back().name == "blend") {
        if (!surfaceMode(*args.back()->value))
            error(locationOf(*args.back()->value), "surface.paint blend must be surface.legacy or surface.source_over");
    }
    if (signature.lowering == BuiltinLowering::Pick || signature.lowering == BuiltinLowering::Choose) {
        const std::size_t first = signature.lowering == BuiltinLowering::Choose ? 1 : 0;
        if (first && typeOf(*args[0]->value) != ValueType::I32)
            error(locationOf(*args[0]->value), "choose seed must be i32");
        std::vector<ValueType> types;
        for (std::size_t i = first; i < args.size(); ++i) types.push_back(typeOf(*args[i]->value));
        const auto result = selectionType(types);
        if (!result) error(call.location, name.name + " choices must have matching storable types or be numeric");
        return *result;
    }
    if (variadicPolygon(signature, call.args.size())) {
        std::vector<ValueType> types;
        for (const auto* arg : args) types.push_back(typeOf(*arg->value));
        // Before 1.0 the color was the first argument.
        if (types.front() == ValueType::Color && isNumeric(types.back()))
            error(locationOf(*args.front()->value),
                  "poly takes its color last, after the vertices: poly(x0, y0, ..., color)");
        for (std::size_t i = 0; i < args.size(); ++i) {
            const auto expected = i + 1 == args.size() ? ValueType::Color : ValueType::F32;
            if (!argFits(expected, types[i]))
                error(locationOf(*args[i]->value), "argument " + std::to_string(i + 1) +
                    " of poly must be " + typeName(expected) + ", got " + typeName(types[i]));
        }
        return ValueType::Void;
    }
    std::vector<ValueType> types;
    const auto* contextual = builtinOverload(signature, args.size());
    if (contextual) for (const auto& candidate : signature.overloads)
        if (contextual && candidate.params.size() == args.size()) for (std::size_t i = 0; i < args.size(); ++i)
            if (candidate.params[i].type != contextual->params[i].type) { contextual = nullptr; break; }
    for (std::size_t i = 0; i < args.size(); ++i) {
        const auto expected = contextual ? std::optional<ValueType>(contextual->params[i].type) : std::nullopt;
        types.push_back(typeOf(*args[i]->value, expected));
    }
    const auto* overload = builtinOverload(signature, types);
    if (overload) {
        const BuiltinSpec concrete{signature.lowering, {*overload}};
        (void)bind(call, builtinCallSignature(concrete, call.args.size(), call.location, name.name), name.name);
        if (name.name == "mask.expand" || name.name == "mask.shrink") {
            const auto radius = known(*args[1]->value);
            if (radius && (std::get<i32>(*radius) < 0 || std::get<i32>(*radius) > kMaxMaskRadius))
                valueError(locationOf(*args[1]->value), "mask radius must be in 0..64");
        }
        return overload->result;
    }
    const auto* arityMatch = builtinOverload(signature, args.size());
    for (std::size_t i = 0; i < args.size(); ++i) {
        const auto expected = arityMatch->params[i].type;
        if (!argFits(expected, types[i]))
            error(locationOf(*args[i]->value), "argument " + std::to_string(i + 1) + " of '" + name.name +
                "' must be " + typeName(expected) + ", got " + typeName(types[i]));
    }
    error(call.location, "no matching overload for '" + name.name + "'");
}

ValueType Typer::typeUserCall(const Identifier& name, const Call& call) {
    if (name.name == "draw")
        error(call.location, "'draw' is an entry block; call a helper or an imported part instead");
    // Resolving a helper checks its body on first use, which recurses here
    // for every call in a chain: the failure messages are built elsewhere.
    const std::optional<FunctionSurface> surface =
        resolveFunction(name.name, call.location, ns_);
    if (!surface.has_value()) failUndefinedFunction(name, call);
    const auto bound = bind(call, helperCallSignature(surface->names), name.name);
    const auto& args = bound.arguments;
    for (std::size_t i = 0; i < args.size(); ++i) {
        const Arg& arg = *args[i];
        const ValueType got = typeOf(*arg.value, surface->params[i]);
        if (!argFits(surface->params[i], got)) failArgument(*arg.value, i, name.name, surface->params[i], got);
    }
    return surface->result.value_or(ValueType::Void);
}

void Typer::failUndefinedFunction(const Identifier& name, const Call& call) const {
    auto candidates = builtinNames();
    for (const auto& function : owner_->asset.functions)
        if (function.name != "draw") candidates.push_back(function.name);
    const auto former = formerBuiltinHint(name.name);
    const auto hint = !former.empty() ? former : name.name == "mix" || name.name == "shade" ?
        "; did you mean 'color." + name.name + "'?" : didYouMean(name.name, candidates);
    error(call.location, "undefined function '" + name.name + "'" + hint);
}

void Typer::failArgument(const Expr& argument, std::size_t index, const std::string& function,
                         ValueType expected, ValueType got) const {
    error(locationOf(argument), "argument " + std::to_string(index + 1) + " of '" + function +
                                    "' must be " + typeName(expected) + ", got " + typeName(got));
}

// `part.draw(seed = seed, at = {x, y}, uniforms...)`
ValueType Typer::typePartCall(const Call& call) {
    const Path path = flatten(*call.callee);
    if (path.base.empty() || path.segments.size() != 1 ||
        path.segments[0] != "draw") {
        error(path.location, "expected 'part.draw(...)'");
    }
    const auto* part = findPart(path.base);
    if (part == nullptr) {
        error(path.location, "'" + path.base + "' is not an imported part" + didYouMean(path.base, partAliases()));
    }

    const auto bound = bind(call, partCallSignature(part->inputs), path.base + ".draw");
    const auto& args = bound.arguments;
    const auto seed = typeOf(*args[kPartSeedSlot]->value);
    if (seed != ValueType::I32)
        error(locationOf(*args[kPartSeedSlot]->value), "part call argument must be an integer, got " +
              std::string(typeName(seed)));
    const auto placement = typeOf(*args[kPartAtSlot]->value);
    if (placement != ValueType::Vec2)
        error(locationOf(*args[kPartAtSlot]->value), "'at' must be a vec2 {x, y}, got " +
              std::string(typeName(placement)));
    for (std::size_t i = kPartTransformBegin; i < kPartTransformEnd; ++i) {
        if (!args[i]) continue;
        const auto got = typeOf(*args[i]->value);
        if (got != ValueType::I32)
            error(locationOf(*args[i]->value), "'" + *args[i]->name +
                  "' must be an integer, got " + std::string(typeName(got)));
    }
    for (std::size_t i=0;i<part->inputs.size();++i) {
        const auto* arg = args[kPartFirstInput + i];
        if (!arg) continue;
        const auto& input = part->inputs[i];
        const auto got = typeOf(*arg->value);
        if (!argFits(valueTypeOf(input.type), got))
            error(locationOf(*arg->value), "input '" + input.name + "' of part '" + path.base +
                  "' must be " + typeName(valueTypeOf(input.type)) + ", got " + typeName(got));
    }
    (void)resolvePartTransform(bound);
    return ValueType::Void;
}

} // namespace iprop::ips
