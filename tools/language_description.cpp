#include "language_description.hpp"
#include "json.hpp"
#include "ips/BuiltinArguments.hpp"
#include "ips/Lexer.hpp"
#include "iprop/OpcodeSpec.hpp"

#include <stdexcept>

namespace iprop::tools {
namespace {
void parameter(std::ostream& out, const std::string& name, const std::string& type) {
    out << "{\"name\":\"" << jsonEscape(name) << "\",\"type\":\"" << jsonEscape(type) << "\"}";
}

void variadic(std::ostream& out, const std::string& name, const ips::BuiltinSpec& spec) {
    using L = ips::BuiltinLowering;
    // Query the actual binder, rather than maintaining another name/arity table.
    std::size_t minimum = spec.lowering == L::Pick ? 2 : 0;
    std::vector<std::string> names;
    for (; minimum < 16; ++minimum) {
        try {
            names = ips::builtinParameterNames(spec, minimum, {1, 1}, name);
            break;
        } catch (const ips::SourceError&) { /* find the first accepted arity */ }
    }
    if (names.empty()) throw std::logic_error("variadic builtin has no accepted arity");
    const bool polygon = spec.lowering == L::Polygon;
    out << ",\"variadic\":{\"minimum_arguments\":" << minimum
        << ",\"repeat\":\"" << (polygon ? "coordinate_pairs" : "choices")
        << "\",\"result\":\"" << (polygon ? "void" : "T") << "\",\"parameters\":[";
    for (std::size_t i = 0; i < names.size(); ++i) {
        if (i) out << ',';
        const std::string type = polygon ? (i == 0 ? "color" : "f32")
            : (spec.lowering == L::Choose && i == 0 ? "i32" : "T");
        parameter(out, names[i], type);
    }
    out << ']';
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
            if (!firstOverload) out << ',';
            firstOverload = false;
            out << "{\"parameters\":[";
            for (std::size_t i = 0; i < overload.params.size(); ++i) {
                if (i) out << ',';
                parameter(out, overload.params[i].name, ips::typeName(overload.params[i].type));
            }
            out << "],\"result\":\"" << ips::typeName(overload.result) << "\"";
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
    const auto names = ips::rampParameterNames();
    out << ",{\"name\":\"ramp\",\"overloads\":[";
    bool firstRamp = true;
    for (const auto type : {ips::ValueType::F32, ips::ValueType::Color}) {
        if (!firstRamp) out << ',';
        firstRamp = false;
        out << "{\"parameters\":[";
        parameter(out, names[0], "palette<" + std::string(ips::typeName(type)) + ">");
        out << ',';
        parameter(out, names[1], "f32");
        out << "],\"result\":\"" << ips::typeName(type) << "\"}";
    }
    out << "]}],\"emoji_aliases\":[";
    first = true;
    for (const auto& alias : ips::emojiAliases()) {
        if (!first) out << ',';
        first = false;
        out << "{\"word\":\"" << alias.word << "\",\"codepoint\":" << static_cast<std::uint32_t>(alias.codepoint) << '}';
    }
    out << "]}\n";
}
} // namespace iprop::tools
