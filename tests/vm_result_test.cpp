#include <catch2/catch_test_macros.hpp>

#include "Compiler.hpp"
#include "iprop/VM.hpp"
#include "ips/Parser.hpp"
#include "ips/Semantics.hpp"
#include "support/compiler_support.hpp"

#include <bit>
#include <stdexcept>

using namespace iprop;

namespace {
void sameFloat(f32 actual, f32 expected) {
    CHECK(std::bit_cast<u32>(actual) == std::bit_cast<u32>(expected));
}
void sameResult(const GenerationResult& actual, const GenerationResult& expected) {
    CHECK(actual.image.width == expected.image.width);
    CHECK(actual.image.height == expected.image.height);
    sameFloat(actual.image.scaleX, expected.image.scaleX);
    sameFloat(actual.image.scaleY, expected.image.scaleY);
    CHECK(actual.image.pixels == expected.image.pixels);
    REQUIRE(actual.outputs.size() == expected.outputs.size());
    for (std::size_t i = 0; i < actual.outputs.size(); ++i) {
        const auto& a = actual.outputs[i];
        const auto& b = expected.outputs[i];
        CHECK(a.name == b.name);
        CHECK(a.kind == b.kind);
        CHECK(a.isList == b.isList);
        REQUIRE(a.points.size() == b.points.size());
        REQUIRE(a.rects.size() == b.rects.size());
        REQUIRE(a.circles.size() == b.circles.size());
        for (std::size_t n = 0; n < a.points.size(); ++n) {
            sameFloat(a.points[n].x, b.points[n].x); sameFloat(a.points[n].y, b.points[n].y);
        }
        for (std::size_t n = 0; n < a.rects.size(); ++n) {
            sameFloat(a.rects[n].x, b.rects[n].x); sameFloat(a.rects[n].y, b.rects[n].y);
            sameFloat(a.rects[n].w, b.rects[n].w); sameFloat(a.rects[n].h, b.rects[n].h);
        }
        for (std::size_t n = 0; n < a.circles.size(); ++n) {
            sameFloat(a.circles[n].x, b.circles[n].x); sameFloat(a.circles[n].y, b.circles[n].y);
            sameFloat(a.circles[n].r, b.circles[n].r);
        }
    }
}
void comparePaths(const std::vector<u8>& bytes) {
    const auto file = loadFile(bytes);
    validate(file);
    for (const u32 seed : {0u, 7u, 0xffffffffu}) {
        GenerationInputs inputs;
        inputs.seed = seed;
        inputs.sizeX = 20;
        inputs.sizeY = 12;
        VM copiedVm(file, inputs), consumedVm(file, inputs);
        copiedVm.run(); consumedVm.run();
        const auto copied = copiedVm.result();
        sameResult(copiedVm.result(), copied); // The old accessor remains reusable.
        const Color* pixels = consumedVm.canvas().pixels.data();
        const u64 remaining = consumedVm.fuel();
        const u64 budget = consumedVm.fuelBudget();
        auto moved = consumedVm.takeResult();
        sameResult(moved, copied);
        CHECK(moved.image.pixels.data() == pixels);
        CHECK(copied.image.pixels.data() != pixels);
        CHECK(remaining == copiedVm.fuel());
        CHECK(budget == copiedVm.fuelBudget());
        CHECK(consumedVm.fuel() == remaining);
        CHECK(consumedVm.fuelBudget() == budget);
        CHECK(consumedVm.debugStack().empty());
        CHECK_THROWS_AS(consumedVm.takeResult(), std::logic_error);
        CHECK_THROWS_AS(consumedVm.result(), std::logic_error);
        CHECK_THROWS_AS(consumedVm.canvas(), std::logic_error);
        CHECK_THROWS_AS(consumedVm.run(), std::logic_error);
        sameResult(moved, copied); // Rejected accesses cannot alter the owner.
    }
}
} // namespace

TEST_CASE("VM consuming and copying result paths preserve all slots and RNG", "[vm-result]") {
    const auto child = ips::parse(R"(asset Child { size 4x4; origin 1,1;
      input f32 offset = -0.0;
      output point anchor; output list rect rectangles; output circle area;
      draw { pixel(1, 1, #fc8); anchor = {offset, unit()};
        rectangles = {0,1,2,3}; area = {1,2,3}; } })");
    const auto root = ips::parse(R"(import child from "child.ips";
      asset Root { size 8x8; output point anchor; output rect bounds;
        output circle area; output list point points; output list rect rectangles;
        output list circle circles; output list circle emptyList; output point unset;
        draw { rect(0,0,8,8,#1238); anchor = {-0.0, unit()}; bounds = {1,2,3,4};
          area = {2,3,4}; for i in 0..3 { points = {i, helper()};
            rectangles = {i,-0.0,unit(),2}; circles = {unit(),i,1}; }
          child.draw(seed = index(n = 99), v = 0, a = 0, at = {4,4}, rot = 90, mirror_x = true);
          points = {unit(), unit()}; }
        helper() -> f32 { return unit(); } })");
    ips::checkAsset(child, {});
    ips::checkAsset(root, {{"child", ips::partInfoOf(child.asset)}});
    comparePaths(compileProgram(root, {{"child", &child}}));
}

TEST_CASE("VM consuming result supports no output declarations", "[vm-result]") {
    comparePaths(compiler_test::compileChecked("asset Empty { size 8x8; draw {} }"));
}

TEST_CASE("VM result consumption requires successful completion", "[vm-result]") {
    auto file = loadFile(compiler_test::compileChecked(
        "asset T { size 8x8; output point p; draw { p = {-0.0, 1}; } }"));
    validate(file);
    VM vm(file, {});
    const auto before = vm.result(); // Preserve the existing pre-run accessor.
    CHECK_THROWS_AS(vm.takeResult(), std::logic_error);
    sameResult(vm.result(), before);
    vm.run();
    CHECK_NOTHROW(vm.run()); // Existing repeat-run no-op remains supported.
    CHECK(vm.takeResult().outputs[0].points.size() == 1);

    file.meta.fuel = 1;
    VM trapped(file, {});
    CHECK_THROWS_AS(trapped.run(), Trap);
    const auto partial = trapped.result();
    CHECK_THROWS_AS(trapped.takeResult(), std::logic_error);
    sameResult(trapped.result(), partial);
}
