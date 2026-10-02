#include "Emit.hpp"

#include <utility>

#include "ips/Fold.hpp"
#include "ips/PartTransform.hpp"

namespace iprop::compiler {

// ---- calls -------------------------------------------------------------------

ips::ValueType Emitter::emitCall(const ips::Call& call) {
    if (const auto* name = std::get_if<ips::Identifier>(call.callee.get())) {
        if (name->name == "ramp") {
            return emitRamp(call);
        }
        if (ips::builtinSpecs().count(name->name) != 0) {
            return emitBuiltin(name->name, call);
        }
        // Bare names resolve to the entry's functions; inside a part, the
        // part's own helpers.
        const auto dot = current_->name.find('.');
        const std::string ns = dot == std::string::npos
                                   ? "" : current_->name.substr(0, dot + 1);
        const auto id = plan_.functionId(name->name, ns);
        if (!id) {
            error(call.location, "unknown function '" + name->name + "'");
        }
        const u32 index = *id;
        const PlannedFunction& callee = plan_.functions[index];
        if (call.args.size() != callee.ast->params.size()) {
            error(call.location, "wrong argument count for '" + name->name + "'");
        }
        for (std::size_t i = 0; i < call.args.size(); ++i) {
            // The callee's declared parameter type drives the widening
            // (i32 -> f32) so the stack matches the function table.
            emitArg(call.args[i],
                    ips::valueTypeOf(callee.ast->params[i].type));
        }
        ins(Opcode::Call, 0, static_cast<u16>(index));
        return callee.langResult.value_or(ips::ValueType::Void);
    }
    if (std::holds_alternative<ips::Member>(*call.callee)) {
        // The dotted builtin names (dist.circle, ...) — the base `dist` is
        // the reserved namespace; anything else is a part call.
        std::vector<std::string> reversed;
        const ips::Expr* current = call.callee.get();
        while (const auto* member = std::get_if<ips::Member>(current)) {
            reversed.push_back(member->name);
            current = member->object.get();
        }
        if (const auto* base = std::get_if<ips::Identifier>(current)) {
            if (!reversed.empty() &&
                (base->name == "dist" || base->name == "noise" ||
                 base->name == "cell" || base->name == "color" || base->name == "vec2")) {
                std::string full = base->name;
                for (auto it = reversed.rbegin(); it != reversed.rend(); ++it) {
                    full += "." + *it;
                }
                if (ips::builtinSpecs().count(full) != 0) {
                    return emitBuiltin(full, call);
                }
            }
        }
        emitPartCall(call);
        return ips::ValueType::Void;
    }
    error(call.location, "cannot call this expression");
}

// Emits one argument and widens i32 to f32 when the builtin expects f32.
void Emitter::emitArg(const ips::Arg& arg, ips::ValueType expected) {
    emitExpr(*arg.value);
    if (expected == ips::ValueType::F32 &&
        current_->typeOf(*arg.value) == ips::ValueType::I32) {
        ins(Opcode::I32ToF32);
    }
}

ips::ValueType Emitter::emitBuiltin(const std::string& name,
                                    const ips::Call& call) {
    using VT = ips::ValueType;
    const auto& spec = ips::builtinSpecs().at(name);
    const auto* overload = ips::builtinOverload(spec, call.args.size());
    if (spec.lowering == ips::BuiltinLowering::Opcode) {
        if (!overload || !overload->opcode) error(call.location, "invalid builtin overload");
        for (std::size_t i = 0; i < call.args.size(); ++i) {
            emitArg(call.args[i], overload->params[i]);
        }
        ins(*overload->opcode);
        return spec.result;
    }
    if (spec.lowering == ips::BuiltinLowering::Round) {
        // round-half-away-from-zero (std::round semantics, spec 8.1):
        // x >= 0 ? trunc(x + 0.5) : -trunc(-x + 0.5). The value is kept in
        // the f32 scratch (blocks have no results, so the arms store the
        // outcome in the i32 scratch instead of yielding it).
        // The i32 scratch takes the lower index (appended first).
        emitArg(call.args[0], VT::F32);
        const auto result = temporary(VT::I32);
        const auto value = temporary(VT::F32);
        const u16 i32Slot = result.index();
        const u16 f32Slot = value.index();
        ins(Opcode::LocalSet, 0, f32Slot);
        ins(Opcode::LocalGet, 0, f32Slot);
        ins(Opcode::ConstF32, 0, static_cast<u16>(plan_.pools.addFloat(0)));
        ins(Opcode::F32Ge);
        ins(Opcode::If);
        ins(Opcode::LocalGet, 0, f32Slot);
        ins(Opcode::ConstF32, 0,
            static_cast<u16>(plan_.pools.addFloat(0x3F000000u))); // 0.5
        ins(Opcode::F32Add);
        ins(Opcode::F32ToI32);
        ins(Opcode::LocalSet, 0, i32Slot);
        ins(Opcode::Else);
        ins(Opcode::LocalGet, 0, f32Slot);
        ins(Opcode::F32Neg);
        ins(Opcode::ConstF32, 0,
            static_cast<u16>(plan_.pools.addFloat(0x3F000000u)));
        ins(Opcode::F32Add);
        ins(Opcode::F32ToI32);
        ins(Opcode::I32Neg);
        ins(Opcode::LocalSet, 0, i32Slot);
        ins(Opcode::End);
        ins(Opcode::LocalGet, 0, i32Slot);
        return VT::I32;
    }
    if (spec.lowering == ips::BuiltinLowering::Polygon) {
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
            const auto expected = input.type == ips::InputType::F32 ? ips::ValueType::F32 :
                input.type == ips::InputType::Color ? ips::ValueType::Color : ips::ValueType::I32;
            emitArg(*overrideArg, expected);
            continue;
        }
        const auto* value = std::get_if<ips::Literal>(input.defaultValue.get());
        Value folded;
        if (value == nullptr || !ips::foldExpression(*value, folded)) {
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
    ins(Opcode::PartDraw, ips::resolvePartTransform(call).kind(), static_cast<u16>(partIndex));
}

} // namespace iprop::compiler
