#include <catch2/catch_test_macros.hpp>
#include "Compiler.hpp"
#include "ConstantEvaluation.hpp"
#include "Pools.hpp"
#include "iprop/VmInternals.hpp"
#include "ips/Parser.hpp"
#include "ips/Semantics.hpp"
#include "support/compiler_support.hpp"

#include <bit>
#include <cmath>
#include <string>
#include <vector>

using namespace iprop;
using namespace compiler_test;
namespace {
std::vector<ips::SourceError> diagnostics(const std::string& source) {
    return ips::checkAssetDiagnostics(ips::parse(source), {});
}
ips::SourceError uncheckedError(const std::string& source) {
    const auto program = ips::parse(source);
    try { (void)compileProgram(program, {}); }
    catch (const ips::SourceError& error) { return error; }
    FAIL("unchecked program compiled");
    throw std::logic_error("unreachable");
}
// `depth` nested layer blocks, each painted after it closes.
std::string nestedLayers(int depth) {
    std::string body = "pixel(0,0,#fff)";
    for (int i = depth - 1; i >= 0; --i)
        body = "layer image" + std::to_string(i) + "{" + body + "}surface.paint(image" + std::to_string(i) + ")";
    return "asset t{size 1x1 draw{" + body + "}}";
}
} // namespace

TEST_CASE("statically known invalid values are reported by the checker in source order", "[static-values]") {
    const std::string bounds = "asset t { size 1x1 draw { a = sampler(5 to 1) b = sampler(9 to 2) "
                               "c = sampler(7 to 3) d = sampler(8 to 4) pixel(0,0,#fff) } }";
    const std::string points = "asset t { size 1x1 draw { a = weighted{1:-1} b = weighted{2:1, 1:1} "
                               "c = weighted{1:0} d = density{2:1, 1:1} pixel(0,0,#fff) } }";
    for (const auto& [source, messages] : std::vector<std::pair<std::string, std::vector<std::string>>>{
             {bounds, {"lower bound exceeds", "lower bound exceeds", "lower bound exceeds", "lower bound exceeds"}},
             {points, {"weights must be nonnegative", "keys must be strictly increasing",
                       "positive total mass", "keys must be strictly increasing"}}}) {
        INFO(source);
        const auto first = diagnostics(source);
        REQUIRE(first.size() == messages.size());
        for (std::size_t i = 0; i < first.size(); ++i) {
            CHECK(first[i].message.find(messages[i]) != std::string::npos);
            CHECK(first[i].code == ips::DiagnosticCode::AssetConstantValue);
            CHECK(first[i].stage == ips::DiagnosticStage::Typing);
            if (i) CHECK(first[i - 1].location.column < first[i].location.column);
        }
        // Planning never reorders or re-derives them, so repeated runs agree.
        for (int run = 0; run < 8; ++run) {
            const auto again = diagnostics(source);
            REQUIRE(again.size() == first.size());
            for (std::size_t i = 0; i < first.size(); ++i) {
                CHECK(again[i].message == first[i].message);
                CHECK(again[i].location.column == first[i].location.column);
            }
        }
    }
}

TEST_CASE("pure traps in runtime expressions stay runtime traps", "[static-values]") {
    // The guarded index would trap, but the branch never runs.
    for (const std::string source : {
             "asset t { size 1x1 const list<i32> L=[1,2,3] draw { s=sampler(1 to 5) "
             "if (list.len(L) > 7) { v = s.pick(L[7]) } pixel(0,0,#fff) } }",
             "asset t { size 1x1 const list<i32> L=[1,2,3] draw { if (0) { s=sampler(L[7] to 9) } } }",
             "asset t { size 1x1 const list<f32> L=[1.0] draw { layer image {} "
             "if (0) { m = mask.from(image).blur(L[4]) } } }"}) {
        INFO(source);
        CHECK(diagnostics(source).empty());
        const auto program = ips::parse(source);
        CHECK_NOTHROW(compileProgram(program, {}));
    }
}

TEST_CASE("description constructors reject known parameters without naming a constant", "[static-values]") {
    const auto errors = diagnostics("asset t { size 1x1 draw { s = sampler(0.0 to 1.0 with gaussian(0, -1)) } }");
    REQUIRE(errors.size() == 1);
    CHECK(errors[0].message == "gaussian needs a finite mean and a finite sigma > 0");
    CHECK(errors[0].code == ips::DiagnosticCode::AssetConstantValue);
    CHECK(errors[0].location.column == 55);
    // A failing argument leaves the constructor unknown: it traps only if reached.
    CHECK(diagnostics("asset t { size 1x1 const list<f32> L=[1.0] draw { if (0) { "
                      "s = sampler(0.0 to 1.0 with gaussian(L[3], 1)) } } }").empty());
}

TEST_CASE("asset parameters shadow palettes in statically known values", "[static-values]") {
    const std::string source = "asset t { size 1x1 output point p palette bounds = [9, 8] "
                               "first(bounds: list<i32>) -> i32 { s = sampler(bounds[0] to 5) return s.pick() } "
                               "draw { p = {first([1, 2]), bounds[1]} } }";
    REQUIRE(diagnostics(source).empty());
    const auto result = execute(compileChecked(source), 3);
    CHECK(result.outputs[0].points[0].x >= 1.f);
    CHECK(result.outputs[0].points[0].x <= 5.f);
    CHECK(result.outputs[0].points[0].y == 8.f);
}

TEST_CASE("layer and clip nesting is bounded where the excess block is written", "[static-values]") {
    const ips::Program fits = ips::parse(nestedLayers(32));
    REQUIRE(ips::checkAssetDiagnostics(fits, {}).empty());
    CHECK_NOTHROW(compileProgram(fits, {}));

    const auto source = nestedLayers(33);
    const auto column = source.find("layer image32") + 1;
    const auto checked = diagnostics(source);
    REQUIRE(checked.size() == 1);
    CHECK(checked[0].code == ips::DiagnosticCode::ResourceLimit);
    CHECK(checked[0].location.column == column);
    CHECK(checked[0].message.find("nest deeper than 32") != std::string::npos);
    const auto planned = uncheckedError(source);
    CHECK(planned.code == ips::DiagnosticCode::ResourceLimit);
    CHECK(planned.stage == ips::DiagnosticStage::Planning);
    CHECK(planned.location.column == column);

    // Clips share the scope stack with layers.
    std::string clipped = "pixel(0,0,#fff)";
    for (int i = 0; i < 33; ++i) clipped = "clip(region){" + clipped + "}";
    const auto clips = diagnostics("asset t{size 1x1 draw{layer image{} region = mask.from(image) " + clipped + "}}");
    REQUIRE(clips.size() == 1);
    CHECK(clips[0].code == ips::DiagnosticCode::ResourceLimit);
}

TEST_CASE("unchecked programs with unstorable slots report a located planning error", "[static-values]") {
    const auto error = uncheckedError("asset t { size 1x1 draw {\n  x = pixel(0, 0, #fff)\n} }");
    CHECK(error.code == ips::DiagnosticCode::PlanError);
    CHECK(error.location.line == 2);
    CHECK(error.message.find("cannot store void in local 'x'") != std::string::npos);
}

TEST_CASE("pool invariant violations are compiler bugs, not resource limits", "[static-values]") {
    const auto internal = [](auto add) {
        compiler::Pools pools;
        try { add(pools); FAIL("invalid pool input accepted"); }
        catch (const compiler::PoolLimit&) { FAIL("invariant reported as a pool limit"); }
        catch (const std::logic_error&) { SUCCEED(); }
    };
    internal([](compiler::Pools& pools) { (void)pools.addSampler(SamplerValue{ValueType::sampler(ValueType::I32), nullptr}); });
    internal([](compiler::Pools& pools) { (void)pools.addStepper(StepperValue{ValueType::stepper(ValueType::I32), nullptr}); });
    internal([](compiler::Pools& pools) { (void)pools.addList(ListValue{ValueType::list(ValueType::I32), nullptr}); });
    internal([](compiler::Pools& pools) { (void)pools.addDistribution(Distribution{}); });
    internal([](compiler::Pools& pools) { (void)pools.typeIndexOf(ValueType::list(ValueType::Color)); });
}

TEST_CASE("constant folding evaluates with the VM and leaves traps and NaNs to it", "[static-values]") {
    const auto bits = [](const Value& value) -> std::vector<u32> {
        if (const auto* i = std::get_if<i32>(&value)) return {static_cast<u32>(*i)};
        if (const auto* f = std::get_if<f32>(&value)) return {std::bit_cast<u32>(*f)};
        const auto v = std::get<Vec2>(value);
        return {std::bit_cast<u32>(v.x), std::bit_cast<u32>(v.y)};
    };
    const auto same = [&](Opcode op, std::vector<Value> args) {
        INFO(static_cast<int>(op));
        const auto folded = compiler::evaluateConstant(op, args);
        REQUIRE(folded);
        CHECK(bits(*folded) == bits(*detail::evaluateConstantOperation(op, args)));
    };
    same(Opcode::I32Clamp, {i32{5}, i32{9}, i32{1}});
    same(Opcode::F32Sin, {0.5f});
    same(Opcode::DistBezierCubic, {Vec2{4, 0}, Vec2{0, 0}, Vec2{1, 0}, Vec2{2, 0}, Vec2{3, 0}});
    same(Opcode::Noise, {0.25f, 0.75f, i32{7}});
    CHECK_FALSE(compiler::evaluateConstant(Opcode::I32DivS, std::vector<Value>{i32{1}, i32{0}}));
    CHECK_FALSE(compiler::evaluateConstant(Opcode::F32Sqrt, std::vector<Value>{-1.f}));
    // Scalar NaNs stay with the VM; vec2 results and component reads keep
    // their computed bits.
    same(Opcode::Vec2Fold, {Vec2{1e20f, 0}, i32{4}});
    same(Opcode::Vec2Y, {Vec2{1.f, std::bit_cast<f32>(0x7fc00000u)}});
    CHECK_FALSE(compiler::evaluateConstant(Opcode::F32Add, std::vector<Value>{std::bit_cast<f32>(0x7f800000u), -std::bit_cast<f32>(0x7f800000u)}));
    CHECK_FALSE(compiler::evaluateConstant(Opcode::DistributionGaussian, std::vector<Value>{0.f, 1.f}));
    CHECK_FALSE(compiler::evaluateConstant(Opcode::DistributionGaussian, std::vector<Value>{0.f, -1.f}));
}
