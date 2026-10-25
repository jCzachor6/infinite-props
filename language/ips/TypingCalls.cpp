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

ValueType Typer::typeCall(const Call& call) {
    if (const auto* name = std::get_if<Identifier>(call.callee.get())) {
        if (name->name == "ramp") {
            return typeRamp(*name, call);
        }
        const auto builtin = builtinSpecs().find(name->name);
        if (builtin != builtinSpecs().end() && !yieldsToHelper(name->name)) {
            return typeBuiltinCall(*name, call, builtin->second);
        }
        return typeUserCall(*name, call);
    }
    if (std::holds_alternative<Member>(*call.callee)) {
        // The dotted builtin names (dist.circle, noise.fbm, ...): the
        // bases dist/noise/cell are the reserved namespaces; anything
        // else is a part call.
        const Path path = flatten(*call.callee);
        if (!path.segments.empty()) {
            std::string full = path.base;
            for (const std::string& segment : path.segments) {
                full += "." + segment;
            }
            const bool reserved = path.base == "dist" || path.base == "noise" ||
                path.base == "cell" || path.base == "color" || path.base == "vec2" ||
                path.base == "surface" || path.base == "mask" || full == "list.len";
            const auto builtin = builtinSpecs().find(full);
            if (reserved && builtin != builtinSpecs().end()) {
                Identifier synthetic{full, path.location};
                return typeBuiltinCall(synthetic, call, builtin->second);
            }
            const auto hint = didYouMean(full, builtinNames());
            if (reserved || !hint.empty()) error(path.location, "undefined builtin '" + full + "'" + hint);
        }
        return typePartCall(call);
    }
    error(call.location, "cannot call this expression");
}

ValueType Typer::typeBuiltinCall(const Identifier& name, const Call& call,
                                 const BuiltinSpec& signature) {
    auto callSignature = builtinCallSignature(signature, call, name.name);
    if (signature.lowering == BuiltinLowering::SurfacePaint) {
        const Arg* image = nullptr;
        for (const auto& arg : call.args) if (arg.name && *arg.name == "image") { image = &arg; break; }
        if (!image) for (const auto& arg : call.args) if (!arg.name) { image = &arg; break; }
        if (image) {
            const auto type = typeOf(*image->value);
            if (type != ValueType::Layer && type != ValueType::Mask)
                error(locationOf(*image->value), "surface.paint image must be layer or mask, got " + std::string(typeName(type)));
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
    if ((signature.lowering == BuiltinLowering::Pick && args.size() >= 2) ||
        signature.lowering == BuiltinLowering::Choose) {
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
        for (std::size_t i = 0; i < args.size(); ++i) {
            const auto expected = i == 0 ? ValueType::Color : ValueType::F32;
            const auto got = typeOf(*args[i]->value);
            if (!argFits(expected, got))
                error(locationOf(*args[i]->value), "argument " + std::to_string(i + 1) +
                    " of poly must be " + typeName(expected) + ", got " + typeName(got));
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

bool Typer::yieldsToHelper(const std::string& name) const {
    if (!builtinYieldsToHelper(name)) return false;
    for (const auto& function : owner_->asset.functions)
        if (function.name == name) return true;
    return false;
}

ValueType Typer::typeUserCall(const Identifier& name, const Call& call) {
    if (name.name == "draw")
        error(call.location, "'draw' is an entry block; call a helper or an imported part instead");
    const std::optional<FunctionSurface> surface =
        resolveFunction(name.name, call.location, ns_);
    if (!surface.has_value()) {
        auto candidates = builtinNames();
        for (const auto& function : owner_->asset.functions)
            if (function.name != "draw") candidates.push_back(function.name);
        const auto hint = name.name == "mix" || name.name == "shade" ?
            "; did you mean 'color." + name.name + "'?" : didYouMean(name.name, candidates);
        error(call.location, "undefined function '" + name.name + "'" + hint);
    }
    const auto bound = bind(call, helperCallSignature(surface->names), name.name);
    const auto& args = bound.arguments;
    for (std::size_t i = 0; i < args.size(); ++i) {
        const Arg& arg = *args[i];
        const ValueType got = typeOf(*arg.value, surface->params[i]);
        if (!argFits(surface->params[i], got)) {
            error(locationOf(*arg.value),
                  "argument " + std::to_string(i + 1) + " of '" + name.name +
                      "' must be " + typeName(surface->params[i]) +
                      ", got " + typeName(got));
        }
    }
    return surface->result.value_or(ValueType::Void);
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
