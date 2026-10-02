#include <catch2/catch_test_macros.hpp>
#include "Plan.hpp"
#include "Emit.hpp"
#include "ips/Parser.hpp"

using namespace iprop;

TEST_CASE("expression typing resolves each call once even when every subtree is queried") {
    std::string expression = "value()";
    constexpr int count = 512;
    for (int i = 1; i < count; ++i) expression += " + value()";
    const auto program = ips::parse("asset t { size 8x8 draw(v:i32,a:i32) { x = " + expression + " } }");
    const auto& function = program.asset.functions[0];
    const auto& root = *std::get<ips::VarDecl>(function.body[0]).value;
    const std::map<std::string, ips::PartInfo> parts;
    ips::Typer typer(parts);
    int resolutions = 0;
    typer.resolveFunction = [&](const std::string&, const ips::SourceLocation&, const std::string& ns) {
        ++resolutions;
        return ips::FunctionSurface{{}, ns == "second." ? ips::ValueType::I32 : ips::ValueType::F32};
    };
    typer.enterFunction(program, function, true, "first.");
    CHECK(typer.typeOf(root) == ips::ValueType::F32);
    CHECK(resolutions == count);
    CHECK(typer.expressions().size() == 2 * count - 1);
    const auto visit = [&](auto&& self, const ips::Expr& node) -> void {
        CHECK(typer.typeOf(node) == ips::ValueType::F32);
        if (const auto* binary = std::get_if<ips::Binary>(&node)) {
            self(self, *binary->left);
            self(self, *binary->right);
        }
    };
    visit(visit, root);
    CHECK(resolutions == count);
    auto saved = typer.saveScope();
    typer.enterFunction(program, function, true, "second.");
    CHECK(typer.typeOf(root) == ips::ValueType::I32);
    CHECK(resolutions == 2 * count);
    typer.restoreScope(std::move(saved));
    CHECK(typer.typeOf(root) == ips::ValueType::F32);
    CHECK(resolutions == 2 * count);
}

TEST_CASE("a completed expression plan survives its type engine and stores pure constants") {
    const auto program = ips::parse(R"(asset t { size 8x8 output point p
      draw(v:i32,a:i32) {
        x = (1 + 2) * 4
        for i in 0..2 { if (i < x) { p = {helper(i), x} } }
      }
      helper(x:i32) -> f32 { return x + 0.5 }
    })");
    auto plan = compiler::Planner(program, {}).plan();
    const auto& first = *std::get<ips::VarDecl>(program.asset.functions[0].body[0]).value;
    CHECK(plan->functions[0].typeOf(first) == ips::ValueType::I32);
    REQUIRE(plan->functions[0].expression(first).constant);
    CHECK(std::get<i32>(*plan->functions[0].expression(first).constant) == 12);
    compiler::Emitter emitter(*plan);
    for (auto& function : plan->functions) CHECK_NOTHROW(emitter.emitFunction(function));
}
