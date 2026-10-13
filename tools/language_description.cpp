#include "language_description.hpp"
#include "json.hpp"
#include "ips/BuiltinArguments.hpp"
#include "ips/CallPlan.hpp"
#include "ips/Typing.hpp"
#include "ips/Version.hpp"
#include "ips/ExportSchema.hpp"
#include "ips/AssetConstants.hpp"
#include "iprop/OpcodeSpec.hpp"
#include "iprop/Collections.hpp"
#include "iprop/Samplers.hpp"
#include "../backends/capi/iprop.h"

#include <algorithm>
#include <ostream>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

namespace iprop::tools {
namespace {
// Json keeps insertion order: the published field order of the schema 1 export.

Json operationEffects(const OperationEffects& effects) {
    Json mayTrap = Json::array();
    for (const auto check : {EffectCheck::NumericRange, EffectCheck::StreamStackUnderflow,
        EffectCheck::PartRenderArguments, EffectCheck::Callee, EffectCheck::PartOutputMissing,
        EffectCheck::DivideByZero, EffectCheck::IntegerOverflow, EffectCheck::PartOutputContext, EffectCheck::IndexRange, EffectCheck::UnassignedHandle}) {
        if ((effects.checks & static_cast<u16>(check)) != 0) mayTrap.push_back(effectCheckName(check));
    }
    return Json::object({{"rng", rngEffectName(effects.rng)}, {"writes_canvas", effects.writesCanvas},
                         {"writes_outputs", effects.writesOutputs},
                         {"summary", effects.conservative ? "conservative" : "intrinsic"},
                         {"may_trap", std::move(mayTrap)}});
}

Json effectRules() {
    auto rules = Json::object({{"intrinsic_scope", "operation_excluding_arguments_and_resources"},
        {"argument_effects", "evaluated_before_operation_in_caller_stream"},
        {"resource_traps", Json::array({"fuel_exhausted", "call_depth_limit", "generation_data_limit"})},
        {"validation_limits", Json::array({"operand_stack_limit"})}, {"allocation_failure", true}});
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
    ips::ValueType::Color, ips::ValueType::Vec2, ips::ValueType::Distribution, ips::ValueType::Circle,
    ips::ValueType::Rect,
    ips::ValueType::list(ips::ValueType::I32), ips::ValueType::list(ips::ValueType::F32),
    ips::ValueType::list(ips::ValueType::Color), ips::ValueType::list(ips::ValueType::Vec2),
    ips::ValueType::list(ips::ValueType::Distribution),
    ips::ValueType::sampler(ips::ValueType::I32), ips::ValueType::sampler(ips::ValueType::F32),
    ips::ValueType::sampler(ips::ValueType::Color), ips::ValueType::sampler(ips::ValueType::Vec2),
    ips::ValueType::list(ips::ValueType::sampler(ips::ValueType::I32)),
    ips::ValueType::list(ips::ValueType::sampler(ips::ValueType::F32)),
    ips::ValueType::list(ips::ValueType::sampler(ips::ValueType::Color)),
    ips::ValueType::list(ips::ValueType::sampler(ips::ValueType::Vec2))};

Json samplers() {
    Json methods = Json::array();
    for (const auto& signature : ips::samplerPickSpec().overloads) {
        Json parameters = Json::array();
        for (std::size_t i = 1; i < signature.params.size(); ++i)
            parameters.push_back(Json::object({{"name", signature.params[i].name}, {"type", ips::typeName(signature.params[i].type)}}));
        const auto* opcode = findOpcode(*signature.opcode);
        methods.push_back(Json::object({{"name", "pick"}, {"receiver", ips::typeName(signature.params[0].type)},
            {"parameters", std::move(parameters)}, {"result", ips::typeName(signature.result)},
            {"opcode", opcode->mnemonic}, {"minimum_format_minor", opcode->minimumMinor},
            {"effects", operationEffects(opcode->effects)}}));
    }
    return Json::object({{"syntax", "sampler(<value> | <lower> to <upper> [with <distribution>])"},
        {"capture_evaluation", "lower_upper_distribution_once"}, {"intrinsic_capture_rng", "none"},
        {"bounds_validation", "capture"}, {"probability_mass_validation", "pick"},
        {"method_evaluation", "receiver_once_before_count_once"}, {"method_resolution", "static_receiver_type_and_arity"},
        {"minimum_count", 0}, {"maximum_count", kMaxListElements}, {"count_type", "i32"},
        {"batch_preflight", "count_handle_allocation_before_first_draw"},
        {"constant_description_draws", 0}, {"empty_batch_draws", 0}, {"unset_empty_batch", "trap"},
        {"capture_effects", operationEffects(findOpcode(Opcode::SamplerRange)->effects)},
        {"methods", std::move(methods)}, {"asset_constants", true}, {"range_palettes", true}});
}

template<class Predicate>
Json types(Predicate include) {
    Json names = Json::array();
    for (const auto type : valueTypes)
        if (include(type)) names.push_back(ips::typeName(type));
    return names;
}

// `lower to upper`: the operator's place in the grammar, its typed lowering
// to the format instructions and their intrinsic effects.
Json randomRange() {
    const auto overload = [](const char* type, Opcode opcode, u32 maximumDraws) {
        const auto* spec = findOpcode(opcode);
        return Json::object({{"lower", type}, {"upper", type}, {"result", type},
            {"opcode", spec->mnemonic}, {"minimum_format_minor", spec->minimumMinor},
            {"bounds", "inclusive"}, {"draws", Json::object({{"minimum", 1}, {"maximum", maximumDraws}})}});
    };
    // Colors and vec2 values: one shared coefficient 0.0 to 1.0 [with d],
    // then the mix of all channels or components.
    const auto ends = [](ips::ValueType type, Opcode opcode, std::initializer_list<const char*> parts) {
        const auto* spec = findOpcode(opcode);
        Json names = Json::array();
        for (const char* part : parts) names.push_back(part);
        return Json::object({{"type", ips::typeName(type)}, {"result", ips::typeName(type)},
            {"coefficient", "shared_unit_interval"}, {"mixed", std::move(names)}, {"opcode", spec->mnemonic},
            {"minimum_format_minor", spec->minimumMinor}, {"effects", operationEffects(spec->effects)}});
    };
    return Json::object({{"operator", "to"}, {"keyword", "contextual"},
        {"precedence", Json::object({{"tighter_than", "<"}, {"looser_than", "<<"}})},
        {"chaining", false}, {"evaluation_order", Json::array({"lower", "upper"})},
        {"overloads", Json::array({overload("i32", Opcode::RngInt, kRangeDraws),
                                   overload("f32", Opcode::RngFloat, 1)})},
        {"mixed_numeric_bounds", "widen_to_f32"},
        {"traps", Json::array({"lower_greater_than_upper", "nonfinite_f32_bound"})},
        {"equal_bounds", "bound_after_one_draw"},
        {"effects", operationEffects(findOpcode(Opcode::RngInt)->effects)},
        {"with", Json::object({{"keyword", "with"}, {"type", ips::typeName(ips::ValueType::Distribution)},
            {"evaluation_order", Json::array({"lower", "upper", "distribution"})},
            {"overloads", Json::array({overload("i32", Opcode::RngIntWith, 1), overload("f32", Opcode::RngFloatWith, 1)})},
            {"truncation", "conditioned_on_range"}, {"integer_rounding", "nearest_half_away_from_zero"},
            {"fuel", "one_plus_kernel_iterations"},
            {"traps", Json::array({"uninitialized_distribution", "equal_bounds_outside_support",
                                   "no_probability_in_range", "kernel_iteration_limit"})},
            {"effects", operationEffects(findOpcode(Opcode::RngIntWith)->effects)}})},
        {"ends", Json::array({ends(ips::ValueType::Color, Opcode::ColorMixRgba, {"r", "g", "b", "a"}),
                              ends(ips::ValueType::Vec2, Opcode::Vec2Mix, {"x", "y"})})}});
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
            {"contexts", Json::array({"numeric_operands", "call_arguments", "helper_returns", "asset_constants"})}}));
    }
    Json typeRules = Json::object();
    typeRules["storable"] = types(ips::isStorable);
    typeRules["helper_parameters"] = types(ips::isStorable);
    typeRules["helper_value_returns"] = types(ips::isStorable);
    typeRules["output_only_geometry"] = types([](ips::ValueType value) { return !ips::isStorable(value); });
    typeRules["point_value_type"] = ips::typeName(ips::ValueType::Vec2);
    typeRules["implicit_widening"] = std::move(widening);
    typeRules["local_reassignment"] = "exact_type";
    // Read-only member reads: one opcode per name, base evaluated once.
    const auto components = [](ips::ValueType type, ips::ValueType result,
                               std::initializer_list<std::pair<const char*, Opcode>> names) {
        Json members = Json::array(), opcodes = Json::array();
        u16 minor = 0;
        for (const auto& [name, opcode] : names) {
            members.push_back(name);
            opcodes.push_back(findOpcode(opcode)->mnemonic);
            minor = std::max(minor, findOpcode(opcode)->minimumMinor);
        }
        return Json::object({{"type", ips::typeName(type)}, {"names", std::move(members)},
            {"result", ips::typeName(result)}, {"assignable", false}, {"opcodes", std::move(opcodes)},
            {"minimum_format_minor", minor}});
    };
    typeRules["parameterized"] = Json::array({Json::object({{"constructor", "list"},
        {"element_types", types([](ips::ValueType type) { return type.isScalar() || type.isSampler(); })},
        {"nested", false}, {"representation", "immutable_arena_handle"}}),
        Json::object({{"constructor", "sampler"}, {"element_types", types([](ips::ValueType type) { return type.isScalar() && type != ips::ValueType::Distribution; })},
        {"nested", false}, {"representation", "immutable_arena_handle"}})});
    typeRules["components"] = Json::array({
        components(ips::ValueType::Vec2, ips::ValueType::F32, {{"x", Opcode::Vec2X}, {"y", Opcode::Vec2Y}}),
        components(ips::ValueType::Color, ips::ValueType::I32,
                   {{"r", Opcode::ColorR}, {"g", Opcode::ColorG}, {"b", Opcode::ColorB}, {"a", Opcode::ColorA}})});
    auto rules = Json::object({{"inputs", std::move(inputs)}, {"types", std::move(typeRules)}});
    // Fixed rules of the current compiler, in their published order.
    rules["locals"] = Json::object({{"declaration", "first_assignment"}, {"scope", "function"},
        {"type_inference_order", "source_traversal"}, {"definite_assignment_analysis", false},
        {"unexecuted_initializer", "typed_zero"}, {"read_requires_prior_assignment", true}});
    rules["visibility"] = Json::object({{"uniforms", "draw_only"}, {"uniforms_read_only", true},
        {"helper_parameters_read_only", true},
        {"draw_controls", Json::object({{"names", Json::array({"v", "a"})}, {"type", "i32"}, {"read_only", true}})},
        {"seed", "current_rng_state_in_all_functions"}, {"palettes", "all_asset_functions"},
        {"constants", "all_asset_functions"}});
    rules["asset_constants"] = Json::object({{"keyword", "contextual"},
        {"syntax", "const <type> <name> = <expression>"}, {"types", types([](ips::ValueType type) { return type.isStorable(); })},
        {"scope", "asset"}, {"visibility", "all_asset_functions"}, {"read_only", true},
        {"dependencies", "forward_acyclic"}, {"evaluation", "compile_time"},
        {"check_all_declarations", true}, {"check_effects_in_dead_branches", true},
        {"user_helper_calls", false}, {"generation_state", false},
        {"distribution_validation", "compile_time"}});
    rules["named_calls"] = Json::object({{"targets", Json::array({"builtin", "helper", "part"})},
        {"mixed_positional_and_named", true}, {"positional_binding", "positional_parameter_ordinal"},
        {"named_arguments_advance_positional_ordinal", false}, {"duplicate_arguments", false},
        {"unknown_arguments", false}});
    rules["restrictions"] = Json::object({{"recursive_helpers", false}, {"nested_part_imports", true},
        {"let_declarations", false}, {"fn_declarations", false}, {"named_geometry_constructors", false}});
    rules["effect_order"] = Json::object({{"binary_operands", "left_to_right"},
        {"logical_operators", "short_circuit_left_to_right"}, {"call_arguments", "signature_evaluation_order"},
        {"named_arguments_change_evaluation_order", false}, {"part_parent_rng", "restored_after_child"}});
    rules["collections"] = Json::object({{"literal", "[<elements>]"}, {"minimum_length", 0}, {"maximum_length", kMaxListElements},
        {"empty_literal", "known_list_type_required"}, {"element_evaluation", "left_to_right_once"},
        {"numeric_elements", "i32_to_f32_widening"}, {"index_type", "i32"}, {"index_bounds", "trap"},
        {"mutable", false}, {"helper_transfer", "share_immutable_data"},
        {"legacy_palette_index", "last_element_fallback"}, {"legacy_palette_length_limit", "unchanged"},
        {"palette_as_list_maximum", kMaxListElements}, {"generation_data_limit_bytes", kMaxGenerationData}});
    rules["samplers"] = samplers();
    rules["palettes"] = Json::object({{"syntax", "palette <name> = [<elements>]"},
        {"element_types", types([](ips::ValueType type) { return type.isSampler() ||
            (type.isScalar() && type != ips::ValueType::Distribution); })},
        {"evaluation", "compile_time_no_rng"}, {"sampling_trigger", "outer_range_or_sampler"},
        {"constant_entries", "lift_to_constant_descriptions_in_sampling_palette"},
        {"numeric_elements", "i32_to_f32_widening"}, {"index_type", "i32"},
        {"index_evaluation", "once"}, {"index_bounds", "last_element_fallback"},
        {"first_class_value", "immutable_list_with_checked_index"},
        {"minimum_length", 1}, {"sampling_maximum_length", kMaxListElements},
        {"first_class_maximum_length", kMaxListElements}, {"legacy_length_limit", "unchanged"},
        {"ramp", "constant_color_or_f32_only"}});
    rules["random_range"] = randomRange();
    rules["point_distributions"] = Json::object({{"syntax", "<family> { <key>: <value>, ... }"},
        {"families", Json::array({Json::object({{"name", "weighted"}, {"minimum_points", 1}, {"maximum_points", 256},
            {"value", "discrete_mass"}, {"equal_bounds", "positive_mass_at_point"}})})},
        {"result", "distribution"}, {"names", "contextual_before_brace"},
        {"operand_types", Json::array({"i32", "f32"})}, {"evaluation", "key_value_pairs_left_to_right_once"},
        {"storage", "immutable_snapshot_original_numeric_types"}, {"intrinsic_rng", "none"},
        {"key_order", "strictly_increasing"}, {"values", "finite_nonnegative_positive_total_mass"},
        {"bounded_draw", "one_stream_step_then_truncate_and_select"}, {"numeric_kernel", "binary64_no_fma"},
        {"integer_rounding", "nearest_half_away_from_zero"}, {"missing_mass", "trap_after_stream_step"},
        {"generation_data_limit_bytes", kMaxGenerationData}});
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
    std::size_t minimum = spec.lowering == L::Polygon ? 7 : spec.lowering == L::Pick ? 2 : 0;
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
        out["choice_types"] = types(ips::isStorable);
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
                {"effects", operationEffects(ips::builtinEffects(spec, &overload))},
                {"constant_evaluable", ips::constantBuiltin(spec, overload)}});
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
            spec.lowering == ips::BuiltinLowering::Polygon) {
            builtin["variadic"] = variadic(name, spec);
            builtin["variadic"]["constant_evaluable"] = false;
        }
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
            {"effects", operationEffects(ramp.effects)}, {"constant_evaluable", true}}));
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
    document["source_rules"] = sourceRules();
    document["effect_rules"] = effectRules();
    // The complete document exists before output: a failure writes nothing.
    out << document.dump() << '\n';
}
} // namespace iprop::tools
