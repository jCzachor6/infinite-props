#pragma once

// Shared helpers of the model tests that compile sources (M3): a source map,
// the compiler, the loader and a generation, with two small skins.

#include <catch2/catch_test_macros.hpp>

#include "SourceCompilation.hpp"
#include "iprop/VM.hpp"
#include "iprop/model/Model.hpp"

#include <bit>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>

namespace model_support {

using namespace iprop;
using Sources = std::map<std::string, std::string>;

inline const char* const kPlain = R"(
asset plain {
  size 8x8
  draw { rect(0, 0, 8, 8, #808080) }
}
)";

inline SourceCompilationResult compile(const Sources& sources, const std::string& entry = "model.ips") {
    SourceCompilationOptions options;
    return compileSource(entry, [&](std::string_view path) {
        const auto found = sources.find(std::string(path));
        if (found == sources.end()) throw std::runtime_error("no source " + std::string(path));
        return found->second;
    }, options);
}

inline model::LoadedModel load(const Sources& sources, const std::string& entry = "model.ips") {
    const auto compiled = compile(sources, entry);
    INFO((compiled.diagnostics.empty() ? std::string("no diagnostic") : compiled.diagnostics.front().message));
    REQUIRE(compiled.succeeded());
    REQUIRE(compiled.model);
    auto loaded = model::loadModel(compiled.bytes);
    model::validateModel(loaded);
    return loaded;
}

inline model::ModelResult generate(const Sources& sources, u32 seed = 7) {
    const auto loaded = load(sources);
    model::ModelInputs inputs;
    inputs.seed = seed;
    return model::generateModel(loaded, inputs);
}

inline std::string diagnostic(const Sources& sources) {
    const auto compiled = compile(sources);
    REQUIRE_FALSE(compiled.succeeded());
    REQUIRE_FALSE(compiled.diagnostics.empty());
    return compiled.diagnostics.front().message;
}

inline std::string trapMessage(const Sources& sources, u32 seed = 1) {
    const auto loaded = load(sources);
    model::ModelInputs inputs;
    inputs.seed = seed;
    try {
        (void)model::generateModel(loaded, inputs);
    } catch (const FormatError& error) {
        return error.what();
    }
    return "no trap";
}

// A render asset named `thing` with the skin `flat` imported.
inline std::string thing(const std::string& body, const std::string& declarations = "", const std::string& helpers = "") {
    return "import flat from \"plain.ips\"\nasset thing {\n  size 40x40x40\n" + declarations + "\n  render {\n" +
           "    s = flat.draw(1)\n" + body + "\n  }\n" + helpers + "\n}\n";
}

inline Sources with(const std::string& source) { return {{"model.ips", source}, {"plain.ips", kPlain}}; }

inline u32 bits(f32 value) { return std::bit_cast<u32>(value); }

// The name is a view taken by value: a reference to a temporary string here
// makes GCC 13 warn that the returned reference may dangle.
inline const model::ModelOutput& output(const model::ModelResult& result, std::string_view name) {
    for (const auto& found : result.outputs)
        if (found.name == name) return found;
    FAIL("no output " << name);
    throw std::logic_error("unreachable");
}

} // namespace model_support
