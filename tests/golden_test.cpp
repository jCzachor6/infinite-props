#include <catch2/catch_test_macros.hpp>

#include "png.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "support/test_support.hpp"

#include "ipa.hpp"

#include "Compiler.hpp"

#include "ips/Parser.hpp"
#include "ips/Semantics.hpp"

#include "iprop/VM.hpp"

// The golden corpus: the semantics freeze (spec §15). Every entry is a
// (program, seed, variant, archetype, size) tuple with a committed golden
// PNG and pinned structured outputs. The images are compared exactly — no
// tolerance — and the outputs bit-exactly.
//
// Regenerate with IPROP_GOLDEN_REGENERATE=1 (only when a change is
// intentional and the spec was updated first).

using namespace iprop;
using namespace test_support;

namespace {

const std::filesystem::path root =
    std::filesystem::path(__FILE__).parent_path().parent_path();

const bool regenerate = std::getenv("IPROP_GOLDEN_REGENERATE") != nullptr;

struct ExpectedOutput {
    std::string name;
    OutputKind kind = OutputKind::Point;
    bool isList = false;
    std::vector<OutputPoint> points;
    std::vector<OutputRect> rects;
    std::vector<OutputCircle> circles;
};

struct GoldenCase {
    const char* source;
    u32 seed = 0;
    u32 variant = 0;
    u32 archetype = 0;
    const char* goldenPng;
    std::vector<ExpectedOutput> outputs;
};

ExpectedOutput pointOutput(std::string name, f32 x, f32 y) {
    ExpectedOutput output;
    output.name = std::move(name);
    output.kind = OutputKind::Point;
    output.points.push_back({x, y});
    return output;
}

// Runs a corpus program and returns its result.
GenerationResult runCase(const GoldenCase& entry) {
    std::ifstream stream(root / entry.source);
    REQUIRE(stream);
    const std::string source((std::istreambuf_iterator<char>(stream)), {});
    const auto bytes = assemble(source);
    auto loaded = loadFile(bytes);
    validate(loaded);
    VM vm(loaded, {entry.seed, 0, 0, {
        {"v", {InputType::U32, {}, 0.f, entry.variant}},
        {"a", {InputType::U32, {}, 0.f, entry.archetype}}}});
    vm.run();
    return vm.result();
}

// Compares a canvas against a golden PNG, pixel by pixel (exact).
void checkImage(const Canvas& canvas, const char* goldenPng) {
    const std::string file = (root / goldenPng).string();
    const auto actual = tools::canvasImage(canvas);
    if (regenerate) {
        REQUIRE(tools::writePng(file, actual));
        return;
    }
    const auto golden = tools::readPng(file);
    REQUIRE(golden.width == canvas.width);
    REQUIRE(golden.height == canvas.height);
    for (std::size_t at = 0; at < golden.rgba.size(); at += 4) {
        const std::size_t pixel = at / 4;
        INFO("pixel " << pixel % canvas.width << "," << pixel / canvas.width);
        REQUIRE(golden.rgba[at] == actual.rgba[at]);
        REQUIRE(golden.rgba[at + 1] == actual.rgba[at + 1]);
        REQUIRE(golden.rgba[at + 2] == actual.rgba[at + 2]);
        REQUIRE(golden.rgba[at + 3] == actual.rgba[at + 3]);
    }
}

// Compares the structured outputs bit-exactly.
void checkOutputs(const GenerationResult& result,
                  const std::vector<ExpectedOutput>& expected) {
    REQUIRE(result.outputs.size() == expected.size());
    for (std::size_t i = 0; i < expected.size(); ++i) {
        const auto& actual = result.outputs[i];
        const auto& want = expected[i];
        INFO("output " << i << " ('" << want.name << "')");
        REQUIRE(actual.name == want.name);
        REQUIRE(actual.kind == want.kind);
        REQUIRE(actual.isList == want.isList);
        REQUIRE(actual.points.size() == want.points.size());
        REQUIRE(actual.rects.size() == want.rects.size());
        REQUIRE(actual.circles.size() == want.circles.size());
        for (std::size_t p = 0; p < want.points.size(); ++p) {
            REQUIRE(actual.points[p].x == want.points[p].x);
            REQUIRE(actual.points[p].y == want.points[p].y);
        }
        for (std::size_t p = 0; p < want.rects.size(); ++p) {
            REQUIRE(actual.rects[p].x == want.rects[p].x);
            REQUIRE(actual.rects[p].y == want.rects[p].y);
            REQUIRE(actual.rects[p].w == want.rects[p].w);
            REQUIRE(actual.rects[p].h == want.rects[p].h);
        }
        for (std::size_t p = 0; p < want.circles.size(); ++p) {
            REQUIRE(actual.circles[p].x == want.circles[p].x);
            REQUIRE(actual.circles[p].y == want.circles[p].y);
            REQUIRE(actual.circles[p].r == want.circles[p].r);
        }
    }
}

const std::vector<GoldenCase> kCorpus = {
    // The flagship: the QuantSpace portrait at a spread of identities.
    {"examples/portrait.ipa", 0, 0, 0, "tests/golden/portrait-seed0.png",
     {pointOutput("pupilCenter", 27.f, 28.f)}},
    {"examples/portrait.ipa", 1, 0, 0, "tests/golden/portrait-seed1.png",
     {pointOutput("pupilCenter", 27.f, 28.f)}},
    {"examples/portrait.ipa", 2, 0, 0, "tests/golden/portrait-seed2.png",
     {pointOutput("pupilCenter", 27.f, 28.f)}},
    {"examples/portrait.ipa", 7, 0, 0, "tests/golden/portrait-seed7.png",
     {pointOutput("pupilCenter", 27.f, 28.f)}},
    {"examples/portrait.ipa", 26, 0, 0, "tests/golden/portrait-seed26.png",
     {pointOutput("pupilCenter", 27.f, 28.f)}},

    // Canvas shapes, translucent blending, weather.
    {"tests/golden/shapes.ipa", 0, 0, 0, "tests/golden/shapes.png",
     {pointOutput("anchor", 20.f, 20.f)}},

    // A per-pixel noise field.
    {"tests/golden/noise.ipa", 0, 0, 0, "tests/golden/noise.png", {}},

    // Parts composition with output propagation. Global slot order: the
    // root output first, then the part's own output.
    {"tests/golden/parts.ipa", 0, 0, 0, "tests/golden/parts.png",
     {pointOutput("center", 8.f, 8.f), pointOutput("spot", 8.f, 8.f)}},
};

} // namespace

TEST_CASE("the golden corpus reproduces every committed image exactly") {
    for (const auto& entry : kCorpus) {
        INFO("program " << entry.source << " seed " << entry.seed);
        const auto result = runCase(entry);
        checkImage(result.image, entry.goldenPng);
        checkOutputs(result, entry.outputs);
    }
}

TEST_CASE("the image comparator detects a single changed pixel") {
    // The comparator must actually fail on a change: if this ever passes
    // vacuously, a real regression could slip through. Self-contained: it
    // builds its own golden bytes instead of reading corpus files.
    const auto result = runCase(kCorpus.back()); // the parts case
    const Canvas& original = result.image;

    std::vector<u8> rgba(static_cast<std::size_t>(original.width) * original.height * 4);
    for (u32 y = 0; y < original.height; ++y) {
        for (u32 x = 0; x < original.width; ++x) {
            const Color& pixel = original.at(x, y);
            const std::size_t at = (static_cast<std::size_t>(y) * original.width + x) * 4;
            rgba[at] = pixel.r;
            rgba[at + 1] = pixel.g;
            rgba[at + 2] = pixel.b;
            rgba[at + 3] = pixel.a;
        }
    }
    const auto pixelsEqual = [&](const Canvas& canvas) {
        for (u32 y = 0; y < canvas.height; ++y) {
            for (u32 x = 0; x < canvas.width; ++x) {
                const Color& p = canvas.at(x, y);
                const std::size_t at = (static_cast<std::size_t>(y) * canvas.width + x) * 4;
                if (rgba[at] != p.r || rgba[at + 1] != p.g || rgba[at + 2] != p.b ||
                    rgba[at + 3] != p.a) {
                    return false;
                }
            }
        }
        return true;
    };

    REQUIRE(pixelsEqual(original)); // sanity: the comparator passes intact
    Canvas tampered = original;
    tampered.at(0, 0).r = static_cast<u8>(tampered.at(0, 0).r ^ 0xFF);
    REQUIRE_FALSE(pixelsEqual(tampered));
}

TEST_CASE("the .ips portrait port reproduces the golden images exactly") {
    // Compile the composed .ips examples (portrait + hair/eyes parts) and
    // check the same seeds as the golden .ipa entries — pixel-exact and
    // with the pupil anchor propagated through eyes.outputs.pupil.
    const auto readFile = [](const std::filesystem::path& path) {
        std::ifstream stream(root / path);
        REQUIRE(stream);
        return std::string((std::istreambuf_iterator<char>(stream)), {});
    };
    const ips::Program hair = ips::parse(readFile("examples/hair.ips"));
    const ips::Program eyes = ips::parse(readFile("examples/eyes.ips"));
    const ips::Program portrait = ips::parse(readFile("examples/portrait.ips"));
    ips::checkAsset(hair, {});
    ips::checkAsset(eyes, {});
    ips::checkAsset(portrait, {{"hair", partInfoOf(hair.asset)},
                               {"eyes", partInfoOf(eyes.asset)}});

    const auto bytes =
        compileProgram(portrait, {{"hair", &hair}, {"eyes", &eyes}});
    auto loaded = loadFile(bytes);
    validate(loaded);

    for (const u32 seed : {0u, 1u, 2u, 7u, 26u}) {
        INFO("seed " << seed);
        VM vm(loaded, {seed, 0, 0, {}});
        vm.run();
        const GenerationResult result = vm.result();
        checkImage(result.image,
                   ("tests/golden/portrait-seed" + std::to_string(seed) + ".png").c_str());
        // The composed asset exposes the root output first, then the part
        // slots (spec 4.3): the propagated pupilCenter plus the eyes part's
        // own pupil anchor.
        checkOutputs(result, {pointOutput("pupilCenter", 27.f, 28.f),
                              pointOutput("pupil", 27.f, 28.f)});
    }
}
