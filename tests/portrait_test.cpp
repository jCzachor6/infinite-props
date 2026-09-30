#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iterator>
#include <string>
#include <set>
#include <vector>

#include "support/test_support.hpp"

#include "ipa.hpp"
#include "Compiler.hpp"
#include "ips/Parser.hpp"
#include "ips/Semantics.hpp"

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

TEST_CASE("portrait inverse CDF selects every age and respects threshold boundaries") {
    const auto root = std::filesystem::path(__FILE__).parent_path().parent_path();
    std::ifstream stream(root / "examples/portrait.ips");
    REQUIRE(stream);
    auto portrait = ips::parse(std::string((std::istreambuf_iterator<char>(stream)), {}));
    auto probe = ips::parse(R"(asset t { size 8x8 input f32 u = 0
        output point sampled
        draw(v: i32, a: i32) { sampled = {ageFromUnit(u), 0} } })");
    const auto helper = std::find_if(portrait.asset.functions.begin(), portrait.asset.functions.end(),
        [](const auto& fn) { return fn.name == "ageFromUnit"; });
    REQUIRE(helper != portrait.asset.functions.end());
    probe.asset.functions.push_back(std::move(*helper));
    ips::checkAsset(probe, {});
    std::ifstream assemblyStream(root / "examples/portrait.ipa");
    REQUIRE(assemblyStream);
    const std::string assembly((std::istreambuf_iterator<char>(assemblyStream)), {});
    const auto start = assembly.find("fn ageFromUnit");
    REQUIRE(start != std::string::npos);
    const auto end = assembly.find("endfn", start);
    const std::string reference = "meta: size=8x8 fuel=100000\n"
        "input: f32 u = 0\noutput: point sampled\n" +
        assembly.substr(start, end + 5 - start) +
        "\nfn draw ( i32, i32, f32 ) -> ( ):\n"
        "local.get 2\ncall ageFromUnit\ni32.to.f32\n"
        "const.f32 0\nout.point sampled\nendfn\n";
    std::vector<LoadedFile> recipes;
    recipes.push_back(loadFile(compileProgram(probe, {})));
    recipes.push_back(loadFile(assemble(reference)));
    for (const auto& loaded : recipes) {
        validate(loaded);
        const auto ageAt = [&](f32 value) {
            UniformValue uniform;
            uniform.type = InputType::F32;
            uniform.f = value;
            VM vm(loaded, GenerationInputs{0, 0, 0, 0, 0, {{"u", uniform}}});
            vm.run();
            return static_cast<i32>(vm.result().outputs[0].points[0].x);
        };
        REQUIRE(ageAt(0.01f) == 18);
        REQUIRE(ageAt(0.5f) == 29);
        REQUIRE(ageAt(0.f) == 18);
        REQUIRE(ageAt(1.f) == 40);
        const f32 thresholds[] = {
            0.02280424168746939f, 0.049674205008708304f, 0.08084390776683863f,
            0.1164408256424196f, 0.15646351621446092f, 0.20076459628511978f,
            0.2490411763163319f, 0.3008343862121714f, 0.35553891819389627f,
            0.412422630991341f, 0.47065530216358203f, 0.529344697836418f,
            0.5875773690086589f, 0.6444610818061037f, 0.6991656137878286f,
            0.7509588236836681f, 0.7992354037148802f, 0.8435364837855391f,
            0.8835591743575805f, 0.9191560922331614f, 0.9503257949912918f,
            0.9771957583125306f};
        for (std::size_t i = 0; i < std::size(thresholds); ++i) {
            INFO("age threshold " << i);
            REQUIRE(ageAt(std::nextafter(thresholds[i], 0.f)) == 18 + static_cast<i32>(i));
            REQUIRE(ageAt(thresholds[i]) == 19 + static_cast<i32>(i));
        }
        std::set<i32> ages;
        for (u32 i = 0; i < 1000; ++i) {
            ages.insert(ageAt(static_cast<f32>(i) / 1000.f));
        }
        REQUIRE(ages.size() == 23);
    }
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

TEST_CASE("the portrait exposes the pupil anchor") {
    const auto loaded = loadExample();
    VM vm(loaded, {0, 0, 0, 0, 0, {}});
    vm.run();
    const auto result = vm.result();
    REQUIRE(result.outputs.size() == 1);
    REQUIRE(result.outputs[0].name == "pupilCenter");
    REQUIRE(result.outputs[0].points[0].x == 27.f);
    REQUIRE(result.outputs[0].points[0].y == 28.f);
}

TEST_CASE("portrait seeds differ; variants do not (QuantSpace parity)") {
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
    // The seed drives the whole identity, exactly like the game.
    REQUIRE_FALSE(same(render(0, 0), render(1, 0)));
    // The game has no variant concept; variants are ignored.
    REQUIRE(same(render(0, 0), render(0, 7)));
}
