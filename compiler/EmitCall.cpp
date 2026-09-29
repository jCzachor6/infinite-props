#include "Emit.hpp"

#include <utility>

#include "Fold.hpp"

namespace iprop::compiler {

// ---- calls -------------------------------------------------------------------

ips::ValueType Emitter::emitCall(const ips::Call& call) {
    if (const auto* name = std::get_if<ips::Identifier>(call.callee.get())) {
        if (ips::builtinSignatures().count(name->name) != 0) {
            return emitBuiltin(name->name, call);
        }
        const u32 index = plan_.functionIndex.at(name->name);
        for (const auto& arg : call.args) {
            emitExpr(*arg.value);
        }
        ins(Opcode::Call, 0, static_cast<u16>(index));
        return plan_.functions[index].langResult.value_or(ips::ValueType::Void);
    }
    if (std::holds_alternative<ips::Member>(*call.callee)) {
        emitPartCall(call);
        return ips::ValueType::Void;
    }
    error(call.location, "cannot call this expression");
}

// Emits one argument and widens i32 to f32 when the builtin expects f32.
void Emitter::emitArg(const ips::Arg& arg, ips::ValueType expected) {
    emitExpr(*arg.value);
    if (expected == ips::ValueType::F32 &&
        plan_.typeEngine().typeOf(*arg.value) == ips::ValueType::I32) {
        ins(Opcode::I32ToF32);
    }
}

ips::ValueType Emitter::emitBuiltin(const std::string& name,
                                    const ips::Call& call) {
    using VT = ips::ValueType;
    const auto emitFixed = [&](std::initializer_list<VT> params,
                               Opcode opcode) -> VT {
        for (std::size_t i = 0; i < call.args.size(); ++i) {
            emitArg(call.args[i], params.begin()[i]);
        }
        ins(opcode);
        return ips::builtinSignatures().at(name).result;
    };

    if (name == "unit") {
        ins(Opcode::RngUnit);
        return VT::F32;
    }
    if (name == "stream_push") {
        emitArg(call.args[0], VT::I32);
        ins(Opcode::StreamPush);
        return VT::Void;
    }
    if (name == "stream_pop") {
        ins(Opcode::StreamPop);
        return VT::Void;
    }
    if (name == "range") {
        emitArg(call.args[0], VT::F32);
        emitArg(call.args[1], VT::F32);
        ins(Opcode::RngRange);
        return VT::F32;
    }
    if (name == "index") return emitFixed({VT::I32}, Opcode::RngIndex);
    if (name == "hash") return emitFixed({VT::I32}, Opcode::Hash);
    if (name == "noise") {
        return emitFixed({VT::F32, VT::F32, VT::I32}, Opcode::Noise);
    }
    if (name == "fault") {
        return emitFixed({VT::F32, VT::F32, VT::I32}, Opcode::Fault);
    }
    if (name == "smooth") return emitFixed({VT::F32}, Opcode::Smooth);
    if (name == "sqrt") return emitFixed({VT::F32}, Opcode::F32Sqrt);
    if (name == "lerp") {
        return emitFixed({VT::F32, VT::F32, VT::F32}, Opcode::Lerp);
    }
    if (name == "mix") {
        return emitFixed({VT::Color, VT::Color, VT::F32}, Opcode::ColorMix);
    }
    if (name == "shade") {
        return emitFixed({VT::Color, VT::I32}, Opcode::ColorShade);
    }
    if (name == "pixel") {
        return emitFixed({VT::I32, VT::I32, VT::Color}, Opcode::Pixel);
    }
    if (name == "rect") {
        return emitFixed({VT::F32, VT::F32, VT::F32, VT::F32, VT::Color},
                         Opcode::Rect);
    }
    if (name == "line") {
        return emitFixed({VT::F32, VT::F32, VT::F32, VT::F32, VT::I32,
                          VT::Color}, Opcode::Line);
    }
    if (name == "oval") {
        return emitFixed({VT::F32, VT::F32, VT::F32, VT::F32, VT::Color},
                         Opcode::Oval);
    }
    if (name == "weather") {
        return emitFixed({VT::I32, VT::I32}, Opcode::Weather);
    }
    if (name == "poly") {
        // Stack order (top first, spec §9.8): colour on top, then the
        // vertex pairs — so the pairs are pushed first, the colour last.
        for (std::size_t i = 1; i < call.args.size(); ++i) {
            emitArg(call.args[i], VT::F32);
        }
        emitArg(call.args[0], VT::Color);
        const u16 vertices = static_cast<u16>((call.args.size() - 1) / 2);
        ins(Opcode::Poly, 0, vertices);
        return VT::Void;
    }
    error(call.location, "undefined function '" + name + "'");
}

// `part.draw(seed, variant, archetype, at = {x, y}, uniforms...)`
void Emitter::emitPartCall(const ips::Call& call) {
    std::vector<std::string> reversed;
    const ips::Expr* current = call.callee.get();
    while (const auto* member = std::get_if<ips::Member>(current)) {
        reversed.push_back(member->name);
        current = member->object.get();
    }
    const auto* base = std::get_if<ips::Identifier>(current);
    const std::string alias = base->name;
    const ips::Program* part = plan_.parts[plan_.partIndex.at(alias)];
    const u32 partIndex = plan_.partIndex.at(alias);

    // Uniforms in declaration order (bottom of the stack): override with the
    // named argument, else push the declared default.
    for (const auto& input : part->asset.inputs) {
        const ips::Arg* overrideArg = nullptr;
        for (const auto& arg : call.args) {
            if (arg.name.has_value() && *arg.name == input.name) {
                overrideArg = &arg;
                break;
            }
        }
        if (overrideArg != nullptr) {
            emitExpr(*overrideArg->value);
            continue;
        }
        const auto* value = std::get_if<ips::Literal>(input.defaultValue.get());
        Value folded;
        if (value == nullptr || !foldExpression(*value, folded)) {
            error(input.location,
                  "part input '" + input.name + "' has no constant default");
        }
        emitConst(folded);
    }

    // seed, variant, archetype (positional, in order).
    for (const auto& arg : call.args) {
        if (!arg.name.has_value()) {
            emitExpr(*arg.value);
        }
    }

    // at = {x, y}
    for (const auto& arg : call.args) {
        if (arg.name.has_value() && *arg.name == "at") {
            emitPointValue(*arg.value);
            break;
        }
    }
    ins(Opcode::PartDraw, 0, static_cast<u16>(partIndex));
}

} // namespace iprop::compiler
