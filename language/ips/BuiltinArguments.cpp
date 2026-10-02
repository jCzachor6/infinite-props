#include "ips/BuiltinArguments.hpp"

#include <algorithm>
#include <set>

namespace iprop::ips {

std::vector<const Arg*> bindArguments(const Call& call, std::span<const std::string> names,
                                      std::string_view function) {
    std::vector<const Arg*> result(names.size(), nullptr);
    std::size_t positional = 0;
    const std::string prefix = "function '" + std::string(function) + "' ";
    for (const auto& arg : call.args) {
        std::size_t index = positional;
        if (arg.name) {
            const auto found = std::find(names.begin(), names.end(), *arg.name);
            if (found == names.end())
                throw SourceError{arg.location, prefix + "has no argument '" + *arg.name + "'"};
            index = static_cast<std::size_t>(found - names.begin());
        } else ++positional;
        if (index >= result.size()) throw SourceError{arg.location, prefix + "has too many positional arguments"};
        if (result[index]) throw SourceError{arg.location, "duplicate '" + names[index] + "' argument"};
        result[index] = &arg;
    }
    for (std::size_t i = 0; i < names.size(); ++i) {
        if (!result[i]) throw SourceError{call.location, prefix + "is missing argument '" + names[i] + "'"};
    }
    return result;
}

std::vector<const Arg*> builtinArguments(const Call& call, const BuiltinSpec& spec,
                                         std::string_view function) {
    std::vector<std::string> names;
    if (spec.lowering == BuiltinLowering::Polygon) {
        if (call.args.size() < 7 || call.args.size() % 2 == 0)
            throw SourceError{call.location, "poly expects a colour and at least three (x, y) vertex pairs"};
        names.push_back("color");
        for (std::size_t i = 1; i < call.args.size(); ++i)
            names.push_back(std::string(i % 2 ? "x" : "y") + std::to_string((i - 1) / 2));
    } else if ((spec.lowering == BuiltinLowering::Pick && call.args.size() >= 2) ||
               spec.lowering == BuiltinLowering::Choose) {
        const std::size_t first = spec.lowering == BuiltinLowering::Choose ? 1 : 0;
        if (first) {
            if (call.args.size() < 2) throw SourceError{call.location, "choose expects a seed and at least one choice"};
            names.push_back("seed");
        }
        for (std::size_t i = first; i < call.args.size(); ++i) names.push_back("value" + std::to_string(i - first));
    } else {
        const auto* overload = builtinOverload(spec, call.args.size());
        if (!overload) {
            std::set<std::size_t> arities;
            for (const auto& candidate : spec.overloads) arities.insert(candidate.params.size());
            std::string expected;
            for (const auto count : arities) {
                if (!expected.empty()) expected += " or ";
                expected += std::to_string(count);
            }
            throw SourceError{call.location, "function '" + std::string(function) + "' expects " + expected +
                " arguments, got " + std::to_string(call.args.size())};
        }
        for (const auto& param : overload->params) names.push_back(param.name);
    }
    return bindArguments(call, names, function);
}

} // namespace iprop::ips
