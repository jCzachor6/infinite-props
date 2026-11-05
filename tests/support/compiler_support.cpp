#include "compiler_support.hpp"
#include "Compiler.hpp"
#include "SourceCompilation.hpp"
#include "ips/Parser.hpp"
#include "ips/Semantics.hpp"
#include "ips/SourceError.hpp"

#include <catch2/catch_test_macros.hpp>

#include <bit>

namespace compiler_test {
using namespace iprop;

std::vector<u8> compileChecked(const std::string& source, bool optimize) {
    const ips::Program program = ips::parse(source);
    ips::checkAsset(program, {});
    return compileProgram(program, {}, optimize);
}

RunOutcome execute(const std::vector<u8>& bytes, u32 seed) {
    const auto loaded = loadFile(bytes);
    validate(loaded);
    VM vm(loaded, GenerationInputs{seed, 0, 0, {}});
    vm.run();
    const GenerationResult result = vm.result();
    return {result.image, result.outputs};
}

RunOutcome run(const std::vector<u8>& bytes, u32 v, u32 a) {
    const auto loaded = loadFile(bytes);
    validate(loaded);
    GenerationInputs inputs;
    inputs.seed = 42;
    UniformValue value;
    value.type = InputType::U32;
    value.u = v;
    inputs.uniforms.emplace("v", value);
    value.u = a;
    inputs.uniforms.emplace("a", value);
    VM vm(loaded, inputs);
    vm.run();
    const GenerationResult result = vm.result();
    return {result.image, result.outputs};
}

GenerationResult generateVariant(const std::vector<u8>& bytes, u32 seed, u32 variant, u32 width, u32 height) {
    const auto loaded = loadFile(bytes);
    validate(loaded);
    VM vm(loaded, GenerationInputs{seed, width, height, {{"v", {InputType::U32, {}, 0.f, variant}}}});
    vm.run();
    return vm.result();
}

void rejectSource(const std::string& source, const std::string& message) {
    try { (void)compileChecked(source); FAIL("invalid source accepted: " << source); }
    catch (const ips::SourceError& error) {
        INFO(source << " -> " << error.what());
        CHECK(std::string(error.what()).find(message) != std::string::npos);
        CHECK(error.location.line > 0); CHECK(error.location.column > 0);
    }
}

void rejectDiagnostic(const std::string& source, const std::string& message) {
    const auto result = compileSource("rejected.ips", [&](std::string_view) { return source; });
    CHECK_FALSE(result.succeeded()); CHECK(result.bytes.empty()); REQUIRE_FALSE(result.diagnostics.empty());
    INFO(source << " -> " << result.diagnostics.front().message);
    CHECK(result.diagnostics.front().message.find(message) != std::string::npos);
    CHECK(result.diagnostics.front().location.has_value());
}

std::string checkErrorMessage(const std::string& source) {
    try { ips::checkAsset(ips::parse(source), {}); }
    catch (const ips::SourceError& error) { return error.message; }
    FAIL("expected a source error: " << source);
    return {};
}

void sameOutcome(const RunOutcome& actual, const RunOutcome& expected) {
    REQUIRE(actual.canvas.width == expected.canvas.width);
    REQUIRE(actual.canvas.height == expected.canvas.height);
    REQUIRE(actual.canvas.pixels == expected.canvas.pixels);
    REQUIRE(actual.outputs.size() == expected.outputs.size());
    for (std::size_t i = 0; i < actual.outputs.size(); ++i) {
        const auto& a = actual.outputs[i].points;
        const auto& b = expected.outputs[i].points;
        REQUIRE(a.size() == b.size());
        for (std::size_t j = 0; j < a.size(); ++j) {
            CHECK(std::bit_cast<u32>(a[j].x) == std::bit_cast<u32>(b[j].x));
            CHECK(std::bit_cast<u32>(a[j].y) == std::bit_cast<u32>(b[j].y));
        }
    }
}

void sameOutcome(const std::vector<u8>& actual, const std::vector<u8>& expected, u32 seed) {
    sameOutcome(execute(actual, seed), execute(expected, seed));
}

} // namespace compiler_test
