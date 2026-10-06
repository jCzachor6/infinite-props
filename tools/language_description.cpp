#include "language_description.hpp"
#include "json.hpp"
#include "ips/BuiltinArguments.hpp"
#include "ips/CallPlan.hpp"
#include "ips/Typing.hpp"
#include "ips/Lexer.hpp"
#include "ips/Version.hpp"
#include "iprop/OpcodeSpec.hpp"
#include "../backends/capi/iprop.h"

#include <stdexcept>

namespace iprop::tools {
namespace {
void operationEffects(std::ostream& out, const OperationEffects& effects) {
    out << "{\"rng\":\"" << rngEffectName(effects.rng)
        << "\",\"writes_canvas\":" << (effects.writesCanvas ? "true" : "false")
        << ",\"writes_outputs\":" << (effects.writesOutputs ? "true" : "false")
        << ",\"summary\":\"" << (effects.conservative ? "conservative" : "intrinsic")
        << "\",\"may_trap\":[";
    bool first = true;
    for (const auto check : {EffectCheck::NumericRange, EffectCheck::StreamStackUnderflow,
        EffectCheck::PartRenderArguments, EffectCheck::Callee, EffectCheck::PartOutputMissing,
        EffectCheck::DivideByZero, EffectCheck::IntegerOverflow, EffectCheck::PartOutputContext}) {
        if ((effects.checks & static_cast<u8>(check)) == 0) continue;
        if (!first) out << ',';
        first = false;
        out << '"' << effectCheckName(check) << '"';
    }
    out << "]}";
}

void effectRules(std::ostream& out) {
    out << R"(,"effect_rules":{"intrinsic_scope":"operation_excluding_arguments_and_resources",
"argument_effects":"evaluated_before_operation_in_caller_stream",
"resource_traps":["fuel_exhausted","call_depth_limit"],"validation_limits":["operand_stack_limit"],
"allocation_failure":true,"helper":)";
    operationEffects(out, findOpcode(Opcode::Call)->effects);
    out << ",\"part\":";
    operationEffects(out, findOpcode(Opcode::PartDraw)->effects);
    out << ",\"seed_read\":";
    operationEffects(out, findOpcode(Opcode::StreamState)->effects);
    out << ",\"output_write\":";
    operationEffects(out, findOpcode(Opcode::OutPoint)->effects);
    out << ",\"output_write_policy\":{\"singular\":\"replace\",\"list\":\"append\"},\"part_output_read\":";
    operationEffects(out, findOpcode(Opcode::PartGetPoint)->effects);
    out << ",\"integer_division\":";
    operationEffects(out, findOpcode(Opcode::I32DivS)->effects);
    out << ",\"integer_remainder\":";
    operationEffects(out, findOpcode(Opcode::I32RemS)->effects);
    out << '}';
}

constexpr ips::ValueType valueTypes[]{ips::ValueType::I32, ips::ValueType::F32,
    ips::ValueType::Color, ips::ValueType::Vec2, ips::ValueType::Circle, ips::ValueType::Rect};

template<class Predicate>
void types(std::ostream& out, Predicate include) {
    out << '[';
    bool first = true;
    for (const auto type : valueTypes) {
        if (!include(type)) continue;
        if (!first) out << ',';
        first = false;
        out << '"' << ips::typeName(type) << '"';
    }
    out << ']';
}

void sourceRules(std::ostream& out) {
    out << ",\"source_rules\":{\"inputs\":[";
    bool first = true;
    for (const auto type : {ips::InputType::Color, ips::InputType::F32,
                            ips::InputType::U32, ips::InputType::I32}) {
        if (!first) out << ',';
        first = false;
        const auto valueType = ips::valueTypeOf(type);
        const auto* sourceType = type == ips::InputType::U32 ? "u32" : ips::typeName(valueType);
        const auto* binaryType = type == ips::InputType::I32 ? "u32" : sourceType;
        out << "{\"source_type\":\"" << sourceType << "\",\"binary_type\":\"" << binaryType
            << "\",\"value_type\":\"" << ips::typeName(valueType) << "\"}";
    }
    out << "],\"types\":{\"storable\":";
    types(out, ips::isStorable);
    out << ",\"helper_parameters\":";
    types(out, [](ips::ValueType value) {
        for (const auto type : {ips::ParamType::I32, ips::ParamType::F32,
                               ips::ParamType::Color, ips::ParamType::Vec2})
            if (ips::valueTypeOf(type) == value) return true;
        return false;
    });
    out << ",\"helper_value_returns\":";
    types(out, ips::isStorable);
    out << ",\"output_only_geometry\":";
    types(out, [](ips::ValueType value) { return !ips::isStorable(value); });
    out << ",\"point_value_type\":\"" << ips::typeName(ips::ValueType::Vec2)
        << "\",\"implicit_widening\":[";
    first = true;
    for (const auto from : valueTypes) for (const auto to : valueTypes) {
        if (from == to || !ips::argFits(to, from)) continue;
        if (!first) out << ',';
        first = false;
        out << "{\"from\":\"" << ips::typeName(from) << "\",\"to\":\"" << ips::typeName(to)
            << "\",\"contexts\":[\"numeric_operands\",\"call_arguments\",\"helper_returns\"]}";
    }
    out << R"(],"local_reassignment":"exact_type"},
"locals":{"declaration":"first_assignment","scope":"function",
"type_inference_order":"source_traversal","definite_assignment_analysis":false,
"unexecuted_initializer":"typed_zero","read_requires_prior_assignment":true},
"visibility":{"uniforms":"draw_only","uniforms_read_only":true,
"helper_parameters_read_only":true,
"draw_controls":{"names":["v","a"],"type":"i32","read_only":true},
"seed":"current_rng_state_in_all_functions","palettes":"all_asset_functions"},
"named_calls":{"targets":["builtin","helper","part"],"mixed_positional_and_named":true,
"positional_binding":"positional_parameter_ordinal",
"named_arguments_advance_positional_ordinal":false,"duplicate_arguments":false,"unknown_arguments":false},
"restrictions":{"recursive_helpers":false,"nested_part_imports":false,
"let_declarations":false,"fn_declarations":false,"named_geometry_constructors":false},
"effect_order":{"binary_operands":"left_to_right","logical_operators":"short_circuit_left_to_right",
"call_arguments":"signature_evaluation_order","named_arguments_change_evaluation_order":false,
"part_parent_rng":"restored_after_child"}})";
}

void parameter(std::ostream& out, const std::string& name, const std::string& type) {
    out << "{\"name\":\"" << jsonEscape(name) << "\",\"type\":\"" << jsonEscape(type) << "\"}";
}

void evaluationOrder(std::ostream& out, const ips::CallSignature& signature) {
    out << ",\"evaluation_order\":[";
    for (std::size_t i = 0; i < signature.evaluationOrder.size(); ++i) {
        if (i) out << ',';
        out << '"' << jsonEscape(signature.parameters[signature.evaluationOrder[i]].name) << '"';
    }
    out << ']';
}

void variadic(std::ostream& out, const std::string& name, const ips::BuiltinSpec& spec) {
    using L = ips::BuiltinLowering;
    // Query the actual binder, rather than maintaining another name/arity table.
    std::size_t minimum = spec.lowering == L::Pick ? 2 : 0;
    ips::CallSignature signature;
    for (; minimum < 16; ++minimum) {
        try {
            signature = ips::builtinCallSignature(spec, minimum, {1, 1}, name);
            break;
        } catch (const ips::SourceError&) { /* find the first accepted arity */ }
    }
    if (signature.parameters.empty()) throw std::logic_error("variadic builtin has no accepted arity");
    const bool polygon = spec.lowering == L::Polygon;
    out << ",\"variadic\":{\"minimum_arguments\":" << minimum
        << ",\"repeat\":\"" << (polygon ? "coordinate_pairs" : "choices")
        << "\",\"result\":\"" << (polygon ? "void" : "T") << "\",\"parameters\":[";
    for (std::size_t i = 0; i < signature.parameters.size(); ++i) {
        if (i) out << ',';
        const std::string type = polygon ? (i == 0 ? "color" : "f32")
            : (spec.lowering == L::Choose && i == 0 ? "i32" : "T");
        parameter(out, signature.parameters[i].name, type);
    }
    out << ']';
    evaluationOrder(out, signature);
    out << ",\"effects\":";
    operationEffects(out, signature.effects);
    if (!polygon) out << ",\"choice_types\":[\"i32\",\"f32\",\"color\",\"vec2\"]"
        << ",\"choice_evaluation\":\"eager_once_before_selection_rng\"";
    out << '}';
}
} // namespace

void writeLanguageDescription(std::ostream& out, std::string_view productVersion) {
    out << "{\"schema_version\":1,\"product_version\":\"" << jsonEscape(productVersion)
        << "\",\"language_version\":{\"major\":" << ips::kLanguageVersionMajor
        << ",\"minor\":" << ips::kLanguageVersionMinor
        << "},\"c_api_version\":{\"major\":" << IPROP_API_VERSION_MAJOR
        << ",\"minor\":" << IPROP_API_VERSION_MINOR
        << "},\"abi_version\":{\"major\":" << IPROP_C_ABI_VERSION_MAJOR
        << ",\"minor\":" << IPROP_C_ABI_VERSION_MINOR
        << "},\"format_version\":{\"major\":" << kVersionMajor << ",\"minor\":" << kVersionMinor
        << "},\"builtins\":[";
    bool first = true;
    for (const auto& [name, spec] : ips::builtinSpecs()) {
        if (!first) out << ',';
        first = false;
        out << "{\"name\":\"" << jsonEscape(name) << "\",\"overloads\":[";
        bool firstOverload = true;
        for (const auto& overload : spec.overloads) {
            const auto signature = ips::builtinCallSignature(spec, overload.params.size(), {1,1}, name);
            if (!firstOverload) out << ',';
            firstOverload = false;
            out << "{\"parameters\":[";
            for (std::size_t i = 0; i < overload.params.size(); ++i) {
                if (i) out << ',';
                parameter(out, signature.parameters[i].name, ips::typeName(overload.params[i].type));
            }
            out << "],\"result\":\"" << ips::typeName(overload.result) << "\"";
            evaluationOrder(out, signature);
            out << ",\"effects\":";
            operationEffects(out, ips::builtinEffects(spec, &overload));
            if (overload.opcode) {
                const auto* opcode = findOpcode(*overload.opcode);
                if (!opcode) throw std::logic_error("builtin has an unknown opcode");
                out << ",\"opcode\":\"" << opcode->mnemonic << "\",\"minimum_format_minor\":" << opcode->minimumMinor;
            }
            out << '}';
        }
        out << ']';
        if (spec.lowering == ips::BuiltinLowering::Pick || spec.lowering == ips::BuiltinLowering::Choose ||
            spec.lowering == ips::BuiltinLowering::Polygon) variadic(out, name, spec);
        out << '}';
    }
    // ramp's palette parameter is a language construct, not a VM stack value.
    const auto ramp = ips::rampCallSignature();
    out << ",{\"name\":\"ramp\",\"overloads\":[";
    bool firstRamp = true;
    for (const auto type : {ips::ValueType::F32, ips::ValueType::Color}) {
        if (!firstRamp) out << ',';
        firstRamp = false;
        out << "{\"parameters\":[";
        parameter(out, ramp.parameters[0].name, "palette<" + std::string(ips::typeName(type)) + ">");
        out << ',';
        parameter(out, ramp.parameters[1].name, "f32");
        out << "],\"result\":\"" << ips::typeName(type) << "\"";
        evaluationOrder(out, ramp);
        out << ",\"effects\":";
        operationEffects(out, ramp.effects);
        out << '}';
    }
    out << "]}],\"call_rules\":{\"helper_evaluation_order\":\"declaration\",\"part\":{";
    // A single placeholder uniform describes the repeatable declaration
    // slot, without creating another list of public control names/defaults.
    const std::vector<ips::PartInput> placeholder{{ips::InputType::F32, "$uniform"}};
    const auto part = ips::partCallSignature(placeholder);
    out << "\"parameters\":[";
    for (std::size_t i = 0; i < part.parameters.size(); ++i) {
        if (i) out << ',';
        const auto& p = part.parameters[i];
        out << "{\"name\":\"" << jsonEscape(p.name) << "\",\"required\":" << (p.required ? "true" : "false")
            << ",\"positional\":" << (p.positional ? "true" : "false");
        if (const auto* value = std::get_if<i32>(&part.defaults[i])) out << ",\"default\":" << *value;
        else if (std::holds_alternative<const ips::InputDecl*>(part.defaults[i]))
            out << ",\"default\":\"input_declaration\"";
        out << '}';
    }
    out << ']';
    evaluationOrder(out, part);
    out << "}},\"emoji_aliases\":[";
    first = true;
    for (const auto& alias : ips::emojiAliases()) {
        if (!first) out << ',';
        first = false;
        out << "{\"word\":\"" << alias.word << "\",\"codepoint\":" << static_cast<std::uint32_t>(alias.codepoint) << '}';
    }
    out << ']';
    sourceRules(out);
    effectRules(out);
    out << "}\n";
}
} // namespace iprop::tools
