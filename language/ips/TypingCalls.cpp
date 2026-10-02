#include "ips/Typing.hpp"
#include "ips/BuiltinArguments.hpp"
#include "ips/PartTransform.hpp"
#include <set>

namespace iprop::ips {

ValueType Typer::typeCall(const Call& call) {
    if (const auto* name = std::get_if<Identifier>(call.callee.get())) {
        if (name->name == "ramp") {
            return typeRamp(*name, call);
        }
        const auto builtin = builtinSpecs().find(name->name);
        if (builtin != builtinSpecs().end()) {
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
             path.base == "cell" || path.base == "color" || path.base == "vec2")) {
            std::string full = path.base;
            for (const std::string& segment : path.segments) {
                full += "." + segment;
            }
            const auto builtin = builtinSpecs().find(full);
            if (builtin != builtinSpecs().end()) {
                Identifier synthetic{full, path.location};
                return typeBuiltinCall(synthetic, call, builtin->second);
            }
        }
        return typePartCall(call);
    }
    error(call.location, "cannot call this expression");
}

ValueType Typer::typeBuiltinCall(const Identifier& name, const Call& call,
                                 const BuiltinSpec& signature) {
    const auto args = builtinArguments(call, signature, name.name);
    if (signature.lowering == BuiltinLowering::Pick && args.size() >= 2) {
        std::vector<ValueType> types;
        for (const auto* arg : args) types.push_back(typeOf(*arg->value));
        const auto result = selectionType(types);
        if (!result) error(call.location, "pick choices must have matching storable types or be numeric");
        return *result;
    }
    if (signature.lowering == BuiltinLowering::Polygon) {
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
    for (const auto* arg : args) types.push_back(typeOf(*arg->value));
    const auto* overload = builtinOverload(signature, types);
    if (overload) return overload->result;
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
    std::set<std::string> named;
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
            if (!named.insert(*arg.name).second)
                error(arg.location, "duplicate '" + *arg.name + "' argument");
            if (*arg.name == "at") {
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
            if (!argFits(valueTypeOf(input->type), got)) {
                error(locationOf(*arg.value),
                      "input '" + *arg.name + "' of part '" + path.base +
                          "' must be " + typeName(valueTypeOf(input->type)) +
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
    (void)resolvePartTransform(call);
    return ValueType::Void;
}

} // namespace iprop::ips
