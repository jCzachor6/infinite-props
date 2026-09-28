#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "support/test_support.hpp"

#include "ipa.hpp"

#include "iprop/VM.hpp"

// The flagship example: a composed portrait with hair/eyes parts, uniforms,
// structured outputs and seeded variants. Pins determinism and the
// composition outputs.

using namespace iprop;
using namespace test_support;

namespace {

LoadedFile loadExample() {
    const auto source = std::filesystem::path(__FILE__).parent_path().parent_path() /
                        "examples/portrait.ipa";
    std::ifstream stream(source);
    REQUIRE(stream);
    const std::string text((std::istreambuf_iterator<char>(stream)), {});
    const auto bytes = assemble(text); // also proves the example stays valid
    auto loaded = loadFile(bytes);
    validate(loaded);
    return loaded;
}

} // namespace

TEST_CASE("the portrait example assembles and validates") {
    REQUIRE_NOTHROW(loadExample());
}

TEST_CASE("two portrait runs are bit-identical (determinism)") {
    const auto loaded = loadExample();
    const auto run = [&](u32 seed, u32 variant) {
        VM vm(loaded, {seed, variant, 0, 0, 0, {}});
        vm.run();
        return vm.result();
    };
    const auto a = run(3, 2);
    const auto b = run(3, 2);
    REQUIRE(a.image.width == b.image.width);
    REQUIRE(a.image.height == b.image.height);
    for (u32 y = 0; y < a.image.height; ++y) {
        for (u32 x = 0; x < a.image.width; ++x) {
            REQUIRE(a.image.at(x, y) == b.image.at(x, y));
        }
    }
}

TEST_CASE("the portrait propagates the pupil anchor") {
    const auto loaded = loadExample();
    VM vm(loaded, {0, 0, 0, 0, 0, {}});
    vm.run();
    const auto result = vm.result();
    REQUIRE(result.outputs.size() == 2);
    REQUIRE(result.outputs[0].name == "pupilCenter");
    REQUIRE(result.outputs[1].name == "pupil");
    // The parent's pupilCenter mirrors the eyes part's pupil output.
    REQUIRE(result.outputs[0].points[0].x == result.outputs[1].points[0].x);
    REQUIRE(result.outputs[0].points[0].y == result.outputs[1].points[0].y);
}

TEST_CASE("portrait variants and seeds differ") {
    const auto loaded = loadExample();
    const auto render = [&](u32 seed, u32 variant) {
        VM vm(loaded, {seed, variant, 0, 0, 0, {}});
        vm.run();
        return vm.result().image;
    };
    const auto same = [&](const Canvas& a, const Canvas& b) {
        for (u32 y = 0; y < a.height; ++y) {
            for (u32 x = 0; x < a.width; ++x) {
                if (a.at(x, y) != b.at(x, y)) {
                    return false;
                }
            }
        }
        return true;
    };
    // Different variants produce different faces (face width tweak).
    REQUIRE_FALSE(same(render(0, 0), render(0, 7)));
    // Different seeds produce different hair (tint tweak).
    REQUIRE_FALSE(same(render(0, 0), render(3, 0)));
}
