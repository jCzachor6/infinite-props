#include "language_description.hpp"
#include "ips/BuiltinArguments.hpp"
#include "ips/CallPlan.hpp"
#include "ips/Typing.hpp"
#include "ips/Lexer.hpp"
#include "ips/Version.hpp"
#include "ips/ExportSchema.hpp"
#include "iprop/OpcodeSpec.hpp"
#include "../backends/capi/iprop.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <ostream>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

namespace iprop::tools {
namespace {
// Insertion order is the published field order of the schema 1 export.
using Json = nlohmann::ordered_json;

Json operationEffects(const OperationEffects& effects) {
    Json mayTrap = Json::array();
    for (const auto check : {EffectCheck::NumericRange, EffectCheck::StreamStackUnderflow,
        EffectCheck::PartRenderArguments, EffectCheck::Callee, EffectCheck::PartOutputMissing,
        EffectCheck::DivideByZero, EffectCheck::IntegerOverflow, EffectCheck::PartOutputContext}) {
        if ((effects.checks & static_cast<u8>(check)) != 0) mayTrap.push_back(effectCheckName(check));
    }
    return Json::object({{"rng", rngEffectName(effects.rng)}, {"writes_canvas", effects.writesCanvas},
                         {"writes_outputs", effects.writesOutputs},
                         {"summary", effects.conservative ? "conservative" : "intrinsic"},
                         {"may_trap", std::move(mayTrap)}});
}

Json effectRules() {
    auto rules = Json::parse(R"({"intrinsic_scope":"operation_excluding_arguments_and_resources",
        "argument_effects":"evaluated_before_operation_in_caller_stream",
        "resource_traps":["fuel_exhausted","call_depth_limit"],"validation_limits":["operand_stack_limit"],
        "allocation_failure":true})");
    rules["helper"] = operationEffects(findOpcode(Opcode::Call)->effects);
    rules["part"] = operationEffects(findOpcode(Opcode::PartDraw)->effects);
    rules["seed_read"] = operationEffects(findOpcode(Opcode::StreamState)->effects);
    rules["output_write"] = operationEffects(findOpcode(Opcode::OutPoint)->effects);
    rules["output_write_policy"] = Json::object({{"singular", "replace"}, {"list", "append"}});
    rules["part_output_read"] = operationEffects(findOpcode(Opcode::PartGetPoint)->effects);
    rules["integer_division"] = operationEffects(findOpcode(Opcode::I32DivS)->effects);
    rules["integer_remainder"] = operationEffects(findOpcode(Opcode::I32RemS)->effects);
    return rules;
}

constexpr ips::ValueType valueTypes[]{ips::ValueType::I32, ips::ValueType::F32,
    ips::ValueType::Color, ips::ValueType::Vec2, ips::ValueType::Circle, ips::ValueType::Rect};

template<class Predicate>
Json types(Predicate include) {
    Json names = Json::array();
    for (const auto type : valueTypes)
        if (include(type)) names.push_back(ips::typeName(type));
    return names;
}

Json sourceRules() {
    Json inputs = Json::array();
    for (const auto type : {ips::InputType::Color, ips::InputType::F32,
                            ips::InputType::U32, ips::InputType::I32}) {
        const auto valueType = ips::valueTypeOf(type);
        const auto* sourceType = type == ips::InputType::U32 ? "u32" : ips::typeName(valueType);
        const auto* binaryType = type == ips::InputType::I32 ? "u32" : sourceType;
        inputs.push_back(Json::object({{"source_type", sourceType}, {"binary_type", binaryType},
                                       {"value_type", ips::typeName(valueType)}}));
    }
    Json widening = Json::array();
    for (const auto from : valueTypes) for (const auto to : valueTypes) {
        if (from == to || !ips::argFits(to, from)) continue;
        widening.push_back(Json::object({{"from", ips::typeName(from)}, {"to", ips::typeName(to)},
            {"contexts", Json::array({"numeric_operands", "call_arguments", "helper_returns"})}}));
    }
    Json typeRules = Json::object();
    typeRules["storable"] = types(ips::isStorable);
    typeRules["helper_parameters"] = types([](ips::ValueType value) {
        for (const auto type : {ips::ParamType::I32, ips::ParamType::F32,
                               ips::ParamType::Color, ips::ParamType::Vec2})
            if (ips::valueTypeOf(type) == value) return true;
        return false;
    });
    typeRules["helper_value_returns"] = types(ips::isStorable);
    typeRules["output_only_geometry"] = types([](ips::ValueType value) { return !ips::isStorable(value); });
    typeRules["point_value_type"] = ips::typeName(ips::ValueType::Vec2);
    typeRules["implicit_widening"] = std::move(widening);
    typeRules["local_reassignment"] = "exact_type";
    auto rules = Json::object({{"inputs", std::move(inputs)}, {"types", std::move(typeRules)}});
    // Fixed rules of the current compiler, in their published order.
    rules.update(Json::parse(R"({
        "locals":{"declaration":"first_assignment","scope":"function",
            "type_inference_order":"source_traversal","definite_assignment_analysis":false,
            "unexecuted_initializer":"typed_zero","read_requires_prior_assignment":true},
        "visibility":{"uniforms":"draw_only","uniforms_read_only":true,
            "helper_parameters_read_only":true,
            "draw_controls":{"names":["v","a"],"type":"i32","read_only":true},
            "seed":"current_rng_state_in_all_functions","palettes":"all_asset_functions"},
        "named_calls":{"targets":["builtin","helper","part"],"mixed_positional_and_named":true,
            "positional_binding":"positional_parameter_ordinal",
            "named_arguments_advance_positional_ordinal":false,"duplicate_arguments":false,
            "unknown_arguments":false},
        "restrictions":{"recursive_helpers":false,"nested_part_imports":false,
            "let_declarations":false,"fn_declarations":false,"named_geometry_constructors":false},
        "effect_order":{"binary_operands":"left_to_right","logical_operators":"short_circuit_left_to_right",
            "call_arguments":"signature_evaluation_order","named_arguments_change_evaluation_order":false,
            "part_parent_rng":"restored_after_child"}})"));
    return rules;
}

Json parameter(const std::string& name, const std::string& type) {
    return Json::object({{"name", name}, {"type", type}});
}

Json evaluationOrder(const ips::CallSignature& signature) {
    Json names = Json::array();
    for (const auto index : signature.evaluationOrder) names.push_back(signature.parameters[index].name);
    return names;
}

Json variadic(const std::string& name, const ips::BuiltinSpec& spec) {
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
    Json parameters = Json::array();
    for (std::size_t i = 0; i < signature.parameters.size(); ++i) {
        const std::string type = polygon ? (i == 0 ? "color" : "f32")
            : (spec.lowering == L::Choose && i == 0 ? "i32" : "T");
        parameters.push_back(parameter(signature.parameters[i].name, type));
    }
    auto out = Json::object({{"minimum_arguments", minimum},
        {"repeat", polygon ? "coordinate_pairs" : "choices"}, {"result", polygon ? "void" : "T"},
        {"parameters", std::move(parameters)}, {"evaluation_order", evaluationOrder(signature)},
        {"effects", operationEffects(signature.effects)}});
    if (!polygon) {
        out["choice_types"] = Json::array({"i32", "f32", "color", "vec2"});
        out["choice_evaluation"] = "eager_once_before_selection_rng";
    }
    return out;
}

Json builtins() {
    Json list = Json::array();
    for (const auto& [name, spec] : ips::builtinSpecs()) {
        Json overloads = Json::array();
        for (const auto& overload : spec.overloads) {
            const auto signature = ips::builtinCallSignature(spec, overload.params.size(), {1,1}, name);
            Json parameters = Json::array();
            for (std::size_t i = 0; i < overload.params.size(); ++i)
                parameters.push_back(parameter(signature.parameters[i].name, ips::typeName(overload.params[i].type)));
            auto entry = Json::object({{"parameters", std::move(parameters)},
                {"result", ips::typeName(overload.result)}, {"evaluation_order", evaluationOrder(signature)},
                {"effects", operationEffects(ips::builtinEffects(spec, &overload))}});
            if (overload.opcode) {
                const auto* opcode = findOpcode(*overload.opcode);
                if (!opcode) throw std::logic_error("builtin has an unknown opcode");
                entry["opcode"] = opcode->mnemonic;
                entry["minimum_format_minor"] = opcode->minimumMinor;
            }
            overloads.push_back(std::move(entry));
        }
        auto builtin = Json::object({{"name", name}, {"overloads", std::move(overloads)}});
        if (spec.lowering == ips::BuiltinLowering::Pick || spec.lowering == ips::BuiltinLowering::Choose ||
            spec.lowering == ips::BuiltinLowering::Polygon) builtin["variadic"] = variadic(name, spec);
        list.push_back(std::move(builtin));
    }
    // ramp's palette parameter is a language construct, not a VM stack value.
    const auto ramp = ips::rampCallSignature();
    Json rampOverloads = Json::array();
    for (const auto type : {ips::ValueType::F32, ips::ValueType::Color}) {
        rampOverloads.push_back(Json::object({
            {"parameters", Json::array({parameter(ramp.parameters[0].name, "palette<" + std::string(ips::typeName(type)) + ">"),
                                        parameter(ramp.parameters[1].name, "f32")})},
            {"result", ips::typeName(type)}, {"evaluation_order", evaluationOrder(ramp)},
            {"effects", operationEffects(ramp.effects)}}));
    }
    list.push_back(Json::object({{"name", "ramp"}, {"overloads", std::move(rampOverloads)}}));
    return list;
}

Json callRules() {
    // A single placeholder uniform describes the repeatable declaration
    // slot, without creating another list of public control names/defaults.
    const std::vector<ips::PartInput> placeholder{{ips::InputType::F32, "$uniform"}};
    const auto part = ips::partCallSignature(placeholder);
    Json parameters = Json::array();
    for (std::size_t i = 0; i < part.parameters.size(); ++i) {
        const auto& p = part.parameters[i];
        auto entry = Json::object({{"name", p.name}, {"required", p.required}, {"positional", p.positional}});
        if (const auto* value = std::get_if<i32>(&part.defaults[i])) entry["default"] = *value;
        else if (std::holds_alternative<const ips::InputDecl*>(part.defaults[i])) entry["default"] = "input_declaration";
        parameters.push_back(std::move(entry));
    }
    return Json::object({{"helper_evaluation_order", "declaration"},
        {"part", Json::object({{"parameters", std::move(parameters)}, {"evaluation_order", evaluationOrder(part)}})}});
}

Json version(unsigned major, unsigned minor) {
    return Json::object({{"major", major}, {"minor", minor}});
}
} // namespace

void writeLanguageDescription(std::ostream& out, std::string_view productVersion) {
    auto document = Json::object();
    document["schema_version"] = ips::kLanguageDescriptionSchemaVersion;
    document["product_version"] = std::string(productVersion);
    document["language_version"] = version(ips::kLanguageVersionMajor, ips::kLanguageVersionMinor);
    document["c_api_version"] = version(IPROP_API_VERSION_MAJOR, IPROP_API_VERSION_MINOR);
    document["abi_version"] = version(IPROP_C_ABI_VERSION_MAJOR, IPROP_C_ABI_VERSION_MINOR);
    document["c_abi_soversion"] = IPROP_C_ABI_SOVERSION;
    document["format_version"] = version(kVersionMajor, kVersionMinor);
    document["builtins"] = builtins();
    document["call_rules"] = callRules();
    Json aliases = Json::array();
    for (const auto& alias : ips::emojiAliases())
        aliases.push_back(Json::object({{"word", alias.word}, {"codepoint", static_cast<std::uint32_t>(alias.codepoint)}}));
    document["emoji_aliases"] = std::move(aliases);
    document["source_rules"] = sourceRules();
    document["effect_rules"] = effectRules();
    // The complete document exists before output: a failure writes nothing.
    out << document.dump() << '\n';
}
} // namespace iprop::tools
