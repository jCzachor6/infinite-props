#include "language_description.hpp"
#include "json.hpp"
#include "ips/BuiltinArguments.hpp"
#include "ips/CallPlan.hpp"
#include "ips/Typing.hpp"
#include "ips/Lexer.hpp"
#include "iprop/OpcodeSpec.hpp"

#include <stdexcept>

namespace iprop::tools {
namespace {
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
    if (!polygon) out << ",\"choice_types\":[\"i32\",\"f32\",\"color\",\"vec2\"]";
    out << '}';
}
} // namespace

void writeLanguageDescription(std::ostream& out, std::string_view productVersion) {
    out << "{\"schema_version\":1,\"product_version\":\"" << productVersion
        << "\",\"format_version\":{\"major\":" << kVersionMajor << ",\"minor\":" << kVersionMinor
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
    out << "]}\n";
}
} // namespace iprop::tools
