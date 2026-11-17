// Material maps (spec material maps v1.2): roughness, metallic, AO and
// emission outputs of a draw asset, published beside the main image.
#include <catch2/catch_test_macros.hpp>
#include "SourceCompilation.hpp"
#include "iprop/Compression.hpp"
#include "iprop/Debug.hpp"
#include "iprop/NormalMap.hpp"
#include "iprop/VM.hpp"
#include "iprop/Validator.hpp"
#include "ips/Parser.hpp"
#include "ips/Printer.hpp"
#include "ipa.hpp"
#include "ipdis.hpp"
#include "support/compiler_support.hpp"

#include <map>
#include <string>
#include <vector>

using namespace iprop;
using compiler_test::compileChecked;
using compiler_test::rejectSource;

namespace {

using Sources = std::map<std::string, std::string>;
std::vector<u8> compileFiles(const Sources& sources, bool optimize = false) {
    SourceCompilationOptions options; options.optimize = optimize; options.compress = false;
    const auto result = compileSource("root.ips", [&](std::string_view path) { return sources.at(std::string(path)); }, options);
    std::string diagnostics;
    for (const auto& diagnostic : result.diagnostics) diagnostics += diagnostic.message + "\n";
    INFO(diagnostics);
    REQUIRE(result.succeeded());
    return result.bytes;
}

struct Generated { GenerationResult result; u64 fuel = 0; u64 work = 0; };
Generated produce(const std::vector<u8>& bytes, u32 seed = 5, bool derive = false) {
    const auto file = loadFile(bytes); validate(file);
    GenerationInputs request; request.seed = seed;
    VM vm(file, request);
    if (derive) vm.deriveNormal({});
    vm.run();
    Generated out;
    out.fuel = vm.fuelBudget() - vm.fuel();
    out.work = vm.workBudget() - vm.work();
    out.result = vm.takeResult();
    return out;
}
std::vector<u8> reds(const Canvas& image) {
    std::vector<u8> out;
    for (const auto& pixel : image.pixels) out.push_back(pixel.r);
    return out;
}
const AuxImage& imageOf(const GenerationResult& result, AuxImageKind kind) {
    for (const auto& image : result.images) if (image.kind == kind) return image;
    FAIL("no image of the kind");
    return result.images.front();
}
std::string trapOf(const std::vector<u8>& bytes) {
    try { (void)produce(bytes); }
    catch (const Trap& error) { return error.what(); }
    return {};
}

} // namespace

TEST_CASE("material map kinds have fixed tags, flags, names and backgrounds", "[material-maps]") {
    CHECK(static_cast<u32>(AuxImageKind::Normal) == 1);
    CHECK(static_cast<u32>(AuxImageKind::Roughness) == 2);
    CHECK(static_cast<u32>(AuxImageKind::Metallic) == 3);
    CHECK(static_cast<u32>(AuxImageKind::Ao) == 4);
    CHECK(static_cast<u32>(AuxImageKind::Emission) == 5);
    CHECK(kAuxImageKinds == 5);
    CHECK(resourceExportFlag(1) == kResourceExportNormal);
    CHECK(resourceExportFlag(2) == kResourceExportRoughness);
    CHECK(resourceExportFlag(3) == kResourceExportMetallic);
    CHECK(resourceExportFlag(4) == kResourceExportAo);
    CHECK(resourceExportFlag(5) == kResourceExportEmission);
    CHECK(kResourceExportImages == 0x1f);
    CHECK(std::string(auxImageKindName(2)) == "roughness");
    CHECK(std::string(auxImageKindName(4)) == "ao");
    CHECK(auxImageKindName(0) == nullptr);
    CHECK(auxImageKindName(6) == nullptr);
    CHECK(materialMapBackground(2) == Color{255, 255, 255, 255});
    CHECK(materialMapBackground(3) == Color{0, 0, 0, 255});
    CHECK(materialMapBackground(4) == Color{255, 255, 255, 255});
    CHECK(materialMapBackground(5) == Color{0, 0, 0, 255});
}

TEST_CASE("material maps are published by kind, painted on their backgrounds", "[material-maps]") {
    // Declared out of order; the normal map stays first.
    const std::string source = "asset t{size 4x2 output emission e output ao a output normal n output metallic m output roughness r draw{"
        "rect(0,0,4,2,#336699)"
        "layer x(alpha=surface.source_over){rect(0,0,2,2,#40404080)pixel(3,1,#c0c0c0)}"
        "layer d(encoding=surface.normal){pixel(0,0,color.normal(0.0,0.0,1.0))}"
        "r=x m=x a=x e=x n=d}}";
    for (const bool optimize : {false, true}) {
        const auto generated = produce(compileChecked(source, optimize));
        const auto& images = generated.result.images;
        REQUIRE(images.size() == 5);
        const std::vector<AuxImageKind> order{AuxImageKind::Normal, AuxImageKind::Roughness, AuxImageKind::Metallic,
                                              AuxImageKind::Ao, AuxImageKind::Emission};
        const std::vector<std::string> names{"n", "r", "m", "a", "e"};
        for (std::size_t i = 0; i < images.size(); ++i) {
            CHECK(images[i].kind == order[i]);
            CHECK(images[i].source == AuxImageSource::Authored);
            CHECK(images[i].name == names[i]);
            CHECK(images[i].image.width == 4);
            CHECK(images[i].image.height == 2);
            for (const auto& pixel : images[i].image.pixels) CHECK(pixel.a == 255);
        }
        // 0x40 at alpha 0x80 over 255 is 159; over 0 it is 32.
        CHECK(reds(imageOf(generated.result, AuxImageKind::Roughness).image) == std::vector<u8>{159, 159, 255, 255, 159, 159, 255, 192});
        CHECK(reds(imageOf(generated.result, AuxImageKind::Metallic).image) == std::vector<u8>{32, 32, 0, 0, 32, 32, 0, 192});
        CHECK(reds(imageOf(generated.result, AuxImageKind::Ao).image) == reds(imageOf(generated.result, AuxImageKind::Roughness).image));
        for (const auto kind : {AuxImageKind::Roughness, AuxImageKind::Metallic, AuxImageKind::Ao})
            for (const auto& pixel : imageOf(generated.result, kind).image.pixels) CHECK((pixel.g == pixel.r && pixel.b == pixel.r));
        // Emission keeps the composite colour.
        const auto& emission = imageOf(generated.result, AuxImageKind::Emission).image;
        CHECK(emission.at(0, 0) == Color{32, 32, 32, 255});
        CHECK(emission.at(2, 0) == Color{0, 0, 0, 255});
        CHECK(emission.at(3, 1) == Color{192, 192, 192, 255});
    }
}

TEST_CASE("publication costs no fuel and one work step per pixel for a scalar map", "[material-maps]") {
    const auto program = [](const std::string& kind) {
        return compileChecked("asset t{size 5x3 output " + kind + " x draw{layer l(alpha=surface.source_over){rect(1,1,2,1,#808080)} x=l}}", false);
    };
    const auto layer = produce(program("layer"));
    const auto emission = produce(program("emission"));
    CHECK(layer.result.images.empty());
    REQUIRE(emission.result.images.size() == 1);
    CHECK(emission.fuel == layer.fuel);
    for (const std::string kind : {"roughness", "metallic", "ao"}) {
        const auto scalar = produce(program(kind));
        REQUIRE(scalar.result.images.size() == 1);
        CHECK(scalar.fuel == layer.fuel);
        CHECK(scalar.work == emission.work + 15);
    }
    // The main image and the outputs never change.
    CHECK(emission.result.image.pixels == layer.result.image.pixels);
}

TEST_CASE("an unassigned map is absent and a derived normal map comes first", "[material-maps]") {
    const auto bytes = compileChecked("asset t{size 3x3 output roughness r output emission e draw{rect(1,1,1,1,#fff)"
                                      "layer l(alpha=surface.source_over){pixel(0,0,#202020)} e=l}}", false);
    const auto plain = produce(bytes);
    REQUIRE(plain.result.images.size() == 1);
    CHECK(plain.result.images[0].kind == AuxImageKind::Emission);
    const auto derived = produce(bytes, 5, true);
    REQUIRE(derived.result.images.size() == 2);
    CHECK(derived.result.images[0].kind == AuxImageKind::Normal);
    CHECK(derived.result.images[0].source == AuxImageSource::Derived);
    CHECK(derived.result.images[1].kind == AuxImageKind::Emission);
    CHECK(derived.result.images[1].image.pixels == plain.result.images[0].image.pixels);
    CHECK(derived.result.image.pixels == plain.result.image.pixels);
    CHECK(derived.fuel == plain.fuel);
}

TEST_CASE("a part's map is read in the caller's coordinates and only the root's is published", "[material-maps]") {
    const Sources sources{
        {"root.ips", "import piece from \"piece.ips\"\nasset t{size 6x4 output roughness r draw{"
                     "piece.draw(seed, at={1,1}, mirror_x=1) r=piece.outputs.gloss}}"},
        {"piece.ips", "asset piece{size 3x2 output roughness gloss output metallic shine draw{rect(0,0,3,2,#888)"
                      "layer g(alpha=surface.source_over){pixel(0,0,#101010)} gloss=g shine=g}}"}};
    for (const bool optimize : {false, true}) {
        const auto generated = produce(compileFiles(sources, optimize));
        REQUIRE(generated.result.images.size() == 1);
        const auto values = reds(generated.result.images[0].image);
        // The part's (0,0) lands at x = 1 + 2 after the mirror, y = 1.
        for (u32 i = 0; i < values.size(); ++i) CHECK(values[i] == (i == 1 * 6 + 3 ? 16 : 255));
    }
}

TEST_CASE("material map outputs have located diagnostics", "[material-maps]") {
    rejectSource("asset t{size 1x1 output roughness a output roughness b draw{}}", "an asset declares at most one roughness map");
    rejectSource("asset t{size 1x1 output metallic a output metallic b draw{}}", "an asset declares at most one metallic map");
    rejectSource("asset t{size 1x1 output ao a output ao b draw{}}", "an asset declares at most one ao map");
    rejectSource("asset t{size 1x1 output emission a output emission b draw{}}", "an asset declares at most one emission map");
    rejectSource("asset t{size 1x1 output list roughness a draw{}}", "resource exports cannot be lists");
    rejectSource("asset t{size 1x1 output metallic m draw{layer x{pixel(0,0,#fff)} m=mask.from(x)}}", "output 'm' expects layer, got mask");
    rejectSource("asset t{size 1x1 output roughness r output layer r draw{}}", "duplicate output 'r'");
    // The kinds stay ordinary names elsewhere, and one of each kind compiles.
    compileChecked("asset t{size 1x1 output roughness r output metallic m output ao a output emission e output normal n draw{"
                   "roughness=1 metallic=2.0 ao=roughness emission=metallic pixel(0,0,#fff)}}", false);
}

TEST_CASE("a direction layer written to a material map traps with the kind's name", "[material-maps]") {
    for (const std::string kind : {"roughness", "metallic", "ao", "emission"}) {
        const auto bytes = compileChecked("asset t{size 2x2 output " + kind +
            " x draw{layer n(encoding=surface.normal){pixel(0,0,color.normal(0.0,0.0,1.0))} x=n}}", false);
        CHECK(trapOf(bytes) == kind + " output needs a colour layer, not a direction layer");
    }
}

TEST_CASE("export records accept one image bit on a layer and reject the rest", "[material-maps]") {
    const auto bytes = compileChecked("asset t{size 1x1 output mask region output layer plain output layer other draw{}}", false);
    const auto section = loadFile(bytes).layout.find(SectionId::ResourceExports)->offset + 4;
    const auto flagged = [&](u32 index, u16 flags, std::vector<u8> data) {
        const std::size_t at = section + index * 12 + 10;
        data[at] = static_cast<u8>(flags); data[at + 1] = static_cast<u8>(flags >> 8);
        return data;
    };
    for (u32 kind = 2; kind <= kAuxImageKinds; ++kind) {
        const auto loaded = loadFile(flagged(1, resourceExportFlag(kind), bytes));
        CHECK(loaded.resourceExports[1].image == kind);
        CHECK_FALSE(loaded.resourceExports[1].normal);
        CHECK_THROWS_AS(loadFile(flagged(0, resourceExportFlag(kind), bytes)), FormatError); // a mask
        CHECK_THROWS_AS(loadFile(flagged(2, resourceExportFlag(kind), flagged(1, resourceExportFlag(kind), bytes))), FormatError);
        // Two kinds on two layers are fine.
        const auto pair = loadFile(flagged(2, resourceExportFlag(kind == 5 ? 2 : kind + 1), flagged(1, resourceExportFlag(kind), bytes)));
        CHECK(pair.resourceExports[2].image != pair.resourceExports[1].image);
    }
    for (const u16 flags : {u16{3}, u16{6}, u16{0x18}, u16{0x1f}, u16{0x20}, u16{0x8000}})
        CHECK_THROWS_AS(loadFile(flagged(1, flags, bytes)), FormatError);
}

TEST_CASE("material map sources survive formatting, tools, debug stripping and compression", "[material-maps]") {
    const std::string source = "asset t{size 4x4 output roughness gloss output ao shade output emission glow draw{"
        "rect(0,0,4,4,#556677)layer g(alpha=surface.source_over){rect(0,0,2,4,#303030)}"
        "layer e(alpha=surface.source_over){pixel(3,3,#ff8000)}gloss=g shade=g glow=e}}";
    const auto bytes = compileChecked(source, false);
    const auto expected = produce(bytes).result.images;
    REQUIRE(expected.size() == 3);
    const auto same = [&](const std::vector<u8>& data) {
        const auto images = produce(data).result.images;
        REQUIRE(images.size() == expected.size());
        for (std::size_t i = 0; i < images.size(); ++i) {
            CHECK(images[i].kind == expected[i].kind);
            CHECK(images[i].name == expected[i].name);
            CHECK(images[i].image.pixels == expected[i].image.pixels);
        }
    };
    same(compileChecked(source, true));

    auto program = ips::parse(source);
    const auto formatted = ips::formatProgram(program);
    CHECK(formatted.find("output roughness gloss") != std::string::npos);
    CHECK(formatted.find("output ao shade") != std::string::npos);
    CHECK(formatted.find("output emission glow") != std::string::npos);
    auto again = ips::parse(formatted);
    CHECK(ips::formatProgram(again) == formatted);
    same(compileChecked(formatted, false));

    const auto stripped = stripDebug(bytes);
    const auto text = disassemble(stripped);
    CHECK(text.find("export: roughness gloss root") != std::string::npos);
    CHECK(text.find("export: ao shade root") != std::string::npos);
    CHECK(text.find("export: emission glow root") != std::string::npos);
    const auto restored = assemble(text);
    CHECK(disassemble(restored) == text);
    same(restored);
    same(stripped);
    same(compressFile(bytes));
    CHECK_THROWS(assemble("meta: size=1x1\ntypes: layer\nexport: glossy a root\n"));
    CHECK_THROWS(assemble("meta: size=1x1\ntypes: mask\nexport: roughness a root\n"));
}
