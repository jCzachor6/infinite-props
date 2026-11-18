#include "ips/Typing.hpp"
#include "ips/BuiltinArguments.hpp"
#include "ips/Suggestions.hpp"
#include "ips/PartTransform.hpp"
#include "ips/AssetConstants.hpp"
#include <algorithm>
#include <array>

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
    if (type == ValueType::Vec3) return "vec3";
    if (type == ValueType::Skin) return "skin";
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
        if (const auto* builtin = findBuiltin(name->name, modelAsset())) {
            if (modelAsset() && !availableInModel(*builtin))
                error(call.location, name->name + " draws pixels and a render asset has no canvas; "
                                                  "import a draw asset and use it as a skin");
            return typeBuiltinCall(*name, call, *builtin);
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
    if (std::get<Member>(*call.callee).name == "initial" && !(path.base == "seed" && path.segments.size() == 1))
        failInitialReceiver(call);
    if (!path.segments.empty()) {
        std::string full = path.base;
        for (const std::string& segment : path.segments) {
            full += "." + segment;
        }
        // Recognized static imports precede builtin spelling suggestions
        // (e.g. pot.draw resembles part.draw).
        if (path.segments.size() == 1 && path.segments[0] == "draw" && findPart(path.base))
            return modelAsset() ? typeSkinCall(call) : typePartCall(call);
        // A 3D part (M3): an imported render asset built where it is called.
        if (path.segments.size() == 1 && path.segments[0] == "render" && findPart(path.base)) {
            if (!modelAsset())
                error(path.location, "'" + path.base + ".render' builds a 3D part; it is available in a render asset");
            return typeModelPartCall(call);
        }
        if (const auto* builtin = findBuiltin(full, modelAsset())) {
            if (modelAsset() && !availableInModel(*builtin))
                error(path.location, full + " needs a canvas and a render asset has none; "
                                            "import a draw asset and use it as a skin");
            Identifier synthetic{full, path.location};
            return typeBuiltinCall(synthetic, call, *builtin);
        }
        if (!modelAsset() && modelBuiltinSpecs().contains(full))
            error(path.location, full + " belongs to render assets; a draw asset has no 3D model");
        const auto former = formerBuiltinHint(full);
        const auto hint = former.empty() ? didYouMean(full, builtinNames()) : former;
        if (isBuiltinNamespace(path.base, modelAsset()) || (!hint.empty() && !namesValue(path.base)))
            error(path.location, "undefined builtin '" + full + "'" + hint);
    }
    // A call on a value: the language has only the statically resolved
    // methods the parser recognizes, so name the function to use instead.
    const auto& member = std::get<Member>(*call.callee);
    if (!findPart(path.base) && (path.base.empty() || namesValue(path.base))) {
        const auto receiver = typeOf(*member.object);
        const auto space = methodNamespace(receiver);
        std::string hint;
        if (!space.empty() && findBuiltin(space + "." + member.name, modelAsset()))
            hint = "; write " + space + "." + member.name + "(...) with the value as its first argument";
        else if (findBuiltin(member.name, modelAsset()))
            hint = "; write " + member.name + "(...) with the value as an argument";
        if (hint.empty() && receiver == ValueType::Layer) {
            std::vector<std::string> methods;
            for (const auto& [name, spec] : surfaceMethodSpecs()) methods.push_back(name);
            hint = didYouMean(member.name, methods);
        }
        error(member.location, std::string(typeName(receiver)) + " has no method '" + member.name + "'" + hint);
    }
    return modelAsset() ? typeSkinCall(call) : typePartCall(call);
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
    if (signature.lowering == BuiltinLowering::NormalColor && args.size() == 3) {
        // A direction that is known and cannot be stored is an error where it
        // is written; an unknown one falls back to flat at run time.
        std::array<f32, 3> axes{};
        bool allKnown = true;
        for (std::size_t i = 0; i < 3 && allKnown; ++i) {
            if (!isNumeric(typeOf(*args[i]->value, ValueType::F32))) { allKnown = false; break; }
            const auto constant = known(*args[i]->value);
            if (!constant) allKnown = false;
            else axes[i] = std::holds_alternative<i32>(*constant) ? static_cast<f32>(std::get<i32>(*constant)) : std::get<f32>(*constant);
        }
        if (allKnown && (normalColorValue(axes[0], axes[1], axes[2]).flat || !(axes[2] > 0.f)))
            valueError(call.location, "color.normal needs a finite nonzero direction with z > 0");
    }
    if (signature.lowering == BuiltinLowering::SurfacePaint) {
        const auto& overload = paintOverload(bound, typeOf(*args.front()->value));
        for (std::size_t i = 0; i < args.size(); ++i) {
            const auto& param = overload.params[i];
            if (param.name == "blend" && !surfaceMode(*args[i]->value)) {
                // Every constant the argument takes, in wire order.
                std::string modes;
                for (std::size_t k = 0; k < kCompositionSpecs.size(); ++k)
                    modes += std::string(k == 0 ? "" : k + 1 == kCompositionSpecs.size() ? " or " : ", ") +
                        "surface." + std::string(kCompositionSpecs[k].name);
                error(locationOf(*args[i]->value), "surface.paint blend must be " + modes);
            }
            const auto got = typeOf(*args[i]->value, param.type);
            if (!parameterAccepts(param, got)) failBuiltinArgument(*args[i]->value, i, name.name, param, got);
        }
        return overload.result;
    }
    if (signature.lowering == BuiltinLowering::Pick || signature.lowering == BuiltinLowering::Choose) {
        const std::size_t first = signature.lowering == BuiltinLowering::Choose ? 1 : 0;
        if (first) checkSeedArgument(*args[0]->value, "choose seed");
        std::vector<ValueType> types;
        for (std::size_t i = first; i < args.size(); ++i) types.push_back(typeOf(*args[i]->value));
        if (std::find(types.begin(), types.end(), ValueType::Seed) != types.end()) failSeedChoice(call, name.name);
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
    std::vector<std::string_view> named;
    for (const auto* arg : args) named.push_back(arg->name ? std::string_view(*arg->name) : std::string_view());
    const auto* overload = builtinOverload(signature, types, named);
    if (overload) {
        if (modelAsset() && overload->textureOp)
            error(call.location, "the periodic overload of " + name.name + " requires an asset with draw");
        const BuiltinSpec concrete{signature.lowering, {*overload}};
        (void)bind(call, builtinCallSignature(concrete, call.args.size(), call.location, name.name), name.name);
        if (overload->textureOp && overload->params.back().name == "tile") {
            const auto tile = known(*args.back()->value);
            if (tile && std::get<i32>(*tile) != 0 && std::get<i32>(*tile) != 1)
                valueError(locationOf(*args.back()->value), name.name + " tile must be true or false");
        }
        if (name.name == "mask.expand" || name.name == "mask.shrink") {
            const auto radius = known(*args[1]->value);
            if (radius && (std::get<i32>(*radius) < 0 || std::get<i32>(*radius) > kMaxMaskRadius))
                valueError(locationOf(*args[1]->value), "mask radius must be in 0..64");
        }
        return overload->result;
    }
    const auto* arityMatch = builtinOverload(signature, args.size());
    for (std::size_t i = 0; i < args.size(); ++i)
        if (!parameterAccepts(arityMatch->params[i], types[i]))
            failBuiltinArgument(*args[i]->value, i, name.name, arityMatch->params[i], types[i]);
    error(call.location, "no matching overload for '" + name.name + "'");
}

ValueType Typer::typeUserCall(const Identifier& name, const Call& call) {
    if (name.name == "draw")
        error(call.location, "'draw' is an entry block; call a helper or an imported part instead");
    if (modelAsset() && name.name == "render")
        error(call.location, "'render' is an entry block; call a helper instead");
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
    if (!modelAsset() && modelBuiltinSpecs().contains(name.name))
        error(call.location, name.name + " builds 3D geometry; it is available in a render asset");
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
                                    "' must be " + typeName(expected) + ", got " + typeName(got) + seedHint(expected, got));
}

void Typer::failBuiltinArgument(const Expr& argument, std::size_t index, const std::string& function,
                                const BuiltinParameter& parameter, ValueType got) const {
    if (parameter.role == ParameterRole::Seed)
        error(locationOf(argument), "argument " + std::to_string(index + 1) + " of '" + function +
                                        "' must be an i32 or a seed, got " + typeName(got));
    failArgument(argument, index, function, parameter.type, got);
}

// Named seeds (v1.1) convert only on request, so say how.
std::string Typer::seedHint(ValueType expected, ValueType got) {
    if (got == ValueType::Seed) return "; int(s) reads a seed's 32 bits";
    if (expected == ValueType::Seed && got == ValueType::I32)
        return "; derive a seed with x.fork(\"name\") or read seed.initial()";
    return {};
}

void Typer::checkSeedArgument(const Expr& argument, const char* what) {
    const auto type = typeOf(argument);
    if (type != ValueType::I32 && type != ValueType::Seed)
        error(locationOf(argument), std::string(what) + " must be an i32 or a seed, got " + typeName(type));
}

void Typer::failSeedChoice(const Call& call, const std::string& function) const {
    error(call.location, function + " cannot choose between seeds; choose an index and fork with it, "
                                    "as in s.fork(index(n))");
}

void Typer::failInitialReceiver(const Call& call) const {
    error(std::get<Member>(*call.callee).location, "only seed.initial() reads the seed this asset or part call "
          "started with; a seed value keeps no history");
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
    checkSeedArgument(*args[kPartSeedSlot]->value, "a part's seed");
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

// `alias.draw(seed, inputs..., size = {w, h})` in a render asset (models
// v1.2): the imported draw asset generated as it would be for a host.
ValueType Typer::typeSkinCall(const Call& call) {
    const Path path = flatten(*call.callee);
    if (path.base.empty() || path.segments.size() != 1 || path.segments[0] != "draw")
        error(path.location, "expected 'alias.draw(...)'");
    const auto* skin = findPart(path.base);
    if (skin == nullptr)
        error(path.location, "'" + path.base + "' is not an imported asset" + didYouMean(path.base, partAliases()));
    if (skin->model)
        error(path.location, "'" + path.base + "' is a render asset; build it with " + path.base +
                                 ".render(seed, ...), and take skins from assets with draw { }");
    for (const auto& arg : call.args)
        if (arg.name && (*arg.name == "at" || *arg.name == "rot" || *arg.name == "mirror_x" || *arg.name == "mirror_y"))
            error(arg.location, "a skin has no placement; remove '" + *arg.name + "'");
    const auto bound = bind(call, skinCallSignature(skin->inputs), path.base + ".draw");
    const auto& args = bound.arguments;
    checkSeedArgument(*args[kSkinSeedSlot]->value, "a skin's seed");
    if (const auto* size = args[kSkinSizeSlot]) {
        const auto got = typeOf(*size->value);
        if (got != ValueType::Vec2)
            error(locationOf(*size->value), "'size' must be a vec2 {width, height} in pixels, got " +
                                                std::string(typeName(got)));
    }
    for (std::size_t i = 0; i < skin->inputs.size(); ++i) {
        const auto* arg = args[kSkinFirstInput + i];
        if (!arg) continue;
        const auto& input = skin->inputs[i];
        const auto got = typeOf(*arg->value);
        if (!argFits(valueTypeOf(input.type), got))
            error(locationOf(*arg->value), "input '" + input.name + "' of '" + path.base + "' must be " +
                                               typeName(valueTypeOf(input.type)) + ", got " + typeName(got));
    }
    return ValueType::Skin;
}

// `alias.render(seed, inputs...)` in a render asset (M3): the imported
// render asset built in the current scope, a statement.
ValueType Typer::typeModelPartCall(const Call& call) {
    const Path path = flatten(*call.callee);
    const auto* part = findPart(path.base);
    if (!part->model)
        error(path.location, "'" + path.base + "' has draw { }: use " + path.base +
                                 ".draw(...) as a skin; render builds an asset with render { }");
    for (const auto& arg : call.args)
        if (arg.name && (*arg.name == "at" || *arg.name == "yaw" || *arg.name == "scale" || *arg.name == "size"))
            error(arg.location, "a 3D part has no '" + *arg.name + "' argument; put the call in a place(...) block");
    const auto bound = bind(call, modelPartCallSignature(part->inputs), path.base + ".render");
    const auto& args = bound.arguments;
    checkSeedArgument(*args[kModelPartSeedSlot]->value, "a part's seed");
    for (std::size_t i = 0; i < part->inputs.size(); ++i) {
        const auto* arg = args[kModelPartFirstInput + i];
        if (!arg) continue;
        const auto& input = part->inputs[i];
        const auto got = typeOf(*arg->value);
        if (!argFits(valueTypeOf(input.type), got))
            error(locationOf(*arg->value), "input '" + input.name + "' of part '" + path.base + "' must be " +
                                               typeName(valueTypeOf(input.type)) + ", got " + typeName(got));
    }
    return ValueType::Void;
}

} // namespace iprop::ips
