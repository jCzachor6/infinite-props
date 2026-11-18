// The texture toolkit end to end (spec texture toolkit v1.2): tiled layers
// and what wraps on them, blend modes and opacity, gradient and levels,
// periodic noise, and what the additions leave alone. The arithmetic of the
// blends, the filters and the periodic kernels has an independent oracle in
// scripts/check_texture_reference.py; these tests hold the properties that
// follow from it and the language around it.
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "Compiler.hpp"
#include "SourceCompilation.hpp"
#include "iprop/VM.hpp"
#include "iprop/Validator.hpp"
#include "ipa.hpp"
#include "ipdis.hpp"
#include "support/compiler_support.hpp"

#include <bit>
#include <cmath>
#include <cstdlib>
#include <string>
#include <vector>

using namespace iprop;
using Catch::Matchers::ContainsSubstring;
using compiler_test::compileChecked;
using compiler_test::rejectSource;

namespace {

struct Run {
    GenerationResult result;
    u64 fuel = 0;
};

Run generate(const std::vector<u8>& bytes, u32 seed = 7) {
    const auto file = loadFile(bytes);
    validate(file);
    VM vm(file, {seed, 0, 0, {}});
    vm.run();
    return {vm.result(), vm.fuelBudget() - vm.fuel()};
}

Run generate(const std::string& source, bool optimize = true) { return generate(compileChecked(source, optimize)); }

std::string trap(const std::string& source) {
    try {
        (void)generate(source);
    } catch (const FormatError& error) {
        return error.what();
    }
    return "no trap";
}

// The image moved by (dx, dy) on its torus.
std::vector<Color> rolled(const Canvas& image, u32 dx, u32 dy) {
    std::vector<Color> out(image.pixels.size());
    for (u32 y = 0; y < image.height; ++y)
        for (u32 x = 0; x < image.width; ++x)
            out[static_cast<std::size_t>((y + dy) % image.height) * image.width + (x + dx) % image.width] =
                image.pixels[static_cast<std::size_t>(y) * image.width + x];
    return out;
}

bool same(const std::vector<Color>& a, const std::vector<Color>& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (a[i].r != b[i].r || a[i].g != b[i].g || a[i].b != b[i].b || a[i].a != b[i].a) return false;
    return true;
}

std::size_t covered(const Canvas& image) {
    std::size_t count = 0;
    for (const auto& pixel : image.pixels) count += pixel.a != 0;
    return count;
}

std::string replaced(std::string text, const std::string& what, const std::string& with) {
    for (auto at = text.find(what); at != std::string::npos; at = text.find(what, at + with.size()))
        text.replace(at, what.size(), with);
    return text;
}

// One scene of every primitive around (CX, CY) in a tiled capture, a nested
// capture that inherits the tiling, and one of the operations that read the
// neighbours of a pixel. Around a corner the scene wraps; around the middle
// it does not, and the two must be the same image half a tile apart.
const char* const kTorus = R"(asset torus {
  size 64x48
  output normal relief
  draw {
    cx = CX
    cy = CY
    layer img(tile = true, alpha = surface.source_over) {
      oval(cx, cy, 14, 9, #c08040)
      rect(cx - 20.0, cy - 2.0, 40, 4, #40a0ff80)
      line(cx - 5.0, cy - 12.0, cx + 9.0, cy + 15.0, 2, #ffffff)
      curve({cx - 18.0, cy + 6.0}, {cx, cy - 20.0}, {cx + 18.0, cy + 6.0}, 3.0, #20ff60)
      poly([{cx - 6.0, cy + 10.0}, {cx + 6.0, cy + 10.0}, {cx, cy + 18.0}], #ff3080)
      pixel(int(cx) - 22, int(cy) - 3, #ffff00)
      layer dot(alpha = surface.source_over) { oval(cx + 25.0, cy - 16.0, 6, 6, #8040ff) }
      surface.paint(dot.blur(1.5))
    }
    relief = img.normal(3.0, 1.5)
    m = mask.from(img)
    layer shown(alpha = surface.source_over) { SHOW }
    surface.paint(shown, blend = surface.source_over)
  }
}
)";

Run torus(bool middle, const std::string& show) {
    auto source = replaced(kTorus, "CX", middle ? "32.0" : "0.0");
    source = replaced(source, "CY", middle ? "24.0" : "0.0");
    return generate(replaced(source, "SHOW", show));
}

} // namespace

TEST_CASE("a tiled capture is a torus for primitives and for what reads a pixel's neighbours", "[textures]") {
    const std::vector<std::pair<const char*, const char*>> shows{
        {"the capture", "surface.paint(img)"},
        {"layer blur", "surface.paint(img.blur(2.5))"},
        {"mask expand, wide disk", "surface.paint(mask.expand(m, 3), #ff0000)"},
        {"mask expand, radius 1", "surface.paint(mask.expand(m, 1), #ffff00)"},
        {"mask shrink", "surface.paint(mask.shrink(m, 2), #00ff00)"},
        {"mask offset", "surface.paint(mask.offset(m, 5, -7), #0000ff)"},
        {"mask blur", "surface.paint(m.blur(2.0), #ffffff)"},
        {"mask algebra", "surface.paint(mask.subtract(mask.expand(m, 2), m), #00ffff)"},
        {"gradient and levels", "surface.paint(img.gradient([#102030, #f0e0a0]).levels(0.1, 0.9, 1.5).blur(1.0))"},
    };
    for (const auto& [name, show] : shows) {
        INFO(name);
        const auto corner = torus(false, show), middle = torus(true, show);
        CHECK(covered(middle.result.image) > 200);
        CHECK(same(rolled(corner.result.image, 32, 24), middle.result.image.pixels));
    }
    // The normal map of a tiled height wraps too.
    const auto corner = torus(false, "surface.paint(img)"), middle = torus(true, "surface.paint(img)");
    REQUIRE(corner.result.images.size() == 1);
    REQUIRE(middle.result.images.size() == 1);
    CHECK(same(rolled(corner.result.images[0].image, 32, 24), middle.result.images[0].image.pixels));
}

TEST_CASE("tiling is inherited by nested captures and never by the root or a part's canvas", "[textures]") {
    const auto count = [](const std::string& body) {
        return covered(generate("asset t{size 16x16 draw{" + body + "}}").result.image);
    };
    // A 6x6 square around the corner: 9 pixels without tiling, all 36 with it.
    const std::string square = "rect(-3, -3, 6, 6, #ffffff)";
    CHECK(count(square) == 9);
    CHECK(count("layer a{" + square + "} surface.paint(a)") == 9);
    CHECK(count("layer a(tile = true){" + square + "} surface.paint(a)") == 36);
    CHECK(count("layer a(tile = false){" + square + "} surface.paint(a)") == 9);
    CHECK(count("layer a(tile = true){layer b{" + square + "} surface.paint(b)} surface.paint(a)") == 36);
    CHECK(count("layer a(tile = true){layer b(tile = false){" + square + "} surface.paint(b)} surface.paint(a)") == 9);
    CHECK(count("layer a(tile = true, alpha = surface.source_over){" + square + "} surface.paint(a)") == 36);
    CHECK(count("layer a(alpha = surface.legacy, tile = true){" + square + "} surface.paint(a)") == 36);
    // A helper draws into the active target, so it wraps with it.
    CHECK(generate("asset t{size 16x16 mark(){rect(-3, -3, 6, 6, #ffffff)} "
                   "draw{layer a(tile = true){mark()} surface.paint(a)}}").result.image.pixels.size() == 256);
    CHECK(covered(generate("asset t{size 16x16 mark(){rect(-3, -3, 6, 6, #ffffff)} "
                           "draw{layer a(tile = true){mark()} surface.paint(a)}}").result.image) == 36);
    // A blur of an untiled layer still repeats its edge pixel; a tiled one wraps.
    const auto edge = [&](const std::string& options) {
        const auto image = generate("asset t{size 16x16 draw{layer a(" + options + "alpha = surface.source_over)"
            "{rect(0, 0, 2, 16, #ffffff)} layer s(alpha = surface.source_over){surface.paint(a.blur(2.0))} "
            "surface.paint(s, blend = surface.source_over)}}").result.image;
        return std::pair<u8, u8>{image.pixels[0].a, image.pixels[15].a};
    };
    CHECK(edge("").second == 0);
    CHECK(edge("tile = true, ").second > 40);
    CHECK(edge("tile = true, ").first < edge("").first);
}

TEST_CASE("layer options have located diagnostics, and a formatted capture keeps its tiling", "[textures]") {
    rejectSource("asset t{size 1x1 draw{layer a(tile = 1){}}}", "true or false for layer tile");
    rejectSource("asset t{size 1x1 draw{layer a(tile = true, tile = false){}}}", "duplicate layer tile option");
    rejectSource("asset t{size 1x1 draw{layer a(wrap = true){}}}", "layer supports only the 'alpha', 'encoding' and 'tile' options");
    rejectSource("asset t{size 1x1 draw{layer a(alpha = surface.multiply){}}}", "layer alpha must be surface.legacy or surface.source_over");
    rejectSource("asset t{size 1x1 draw{layer a(alpha = surface.legacy, encoding = surface.normal){}}}",
                 "a layer takes either 'alpha' or 'encoding', not both");
    // The tiling of a direction capture, on the wire: mode 3 with bit 0x80.
    const auto bytes = compileChecked("asset t{size 4x4 draw{layer n(encoding = surface.normal, tile = true){} "
                                      "layer f(tile = false){}}}", false);
    const auto text = disassemble(bytes);
    CHECK_THAT(text, ContainsSubstring("layer.begin 0 131"));
    CHECK_THAT(text, ContainsSubstring("layer.begin 1 64"));
    CHECK(assemble(text) == assemble(disassemble(assemble(text))));
}

TEST_CASE("a new blend mode is a W3C blend in straight alpha on any target", "[textures]") {
    const auto pixel = [](const std::string& body) {
        return generate("asset t{size 1x1 draw{" + body + "}}").result.image.pixels[0];
    };
    const std::string over = "layer d(alpha = surface.source_over){pixel(0, 0, #c86432)} surface.paint(d) "
                             "layer s(alpha = surface.source_over){pixel(0, 0, #6496c8)} ";
    // Opaque over opaque: the blend function alone, rounded to nearest.
    const auto multiply = pixel(over + "surface.paint(s, blend = surface.multiply)");
    CHECK(multiply.r == (200 * 100 + 127) / 255);
    CHECK(multiply.g == (100 * 150 + 127) / 255);
    CHECK(multiply.b == (50 * 200 + 127) / 255);
    CHECK(multiply.a == 255);
    const auto screen = pixel(over + "surface.paint(s, blend = surface.screen)");
    CHECK(screen.r == 255 - (55 * 155 + 127) / 255);
    const auto overlay = pixel(over + "surface.paint(s, blend = surface.overlay)");
    CHECK(overlay.r == 255 - (2 * 55 * 155 + 127) / 255); // destination 200 is in the upper half
    CHECK(overlay.b == (2 * 50 * 200 + 127) / 255);        // destination 50 in the lower
    const auto add = pixel(over + "surface.paint(s, blend = surface.add)");
    CHECK(add.r == 255);
    CHECK(add.b == 250);
    const auto subtract = pixel(over + "surface.paint(s, blend = surface.subtract)");
    CHECK(subtract.r == 100);
    CHECK(subtract.g == 0);
    // Over nothing a blend is the source itself, also on the legacy root.
    const auto alone = pixel("layer s(alpha = surface.source_over){pixel(0, 0, #6496c880)} surface.paint(s, blend = surface.multiply)");
    CHECK(alone.r == 100);
    CHECK(alone.a == 128);
    // Opacity scales the source alpha: none leaves the target, all is the plain paint.
    const auto none = pixel(over + "surface.paint(s, blend = surface.multiply, opacity = 0.0)");
    CHECK(none.r == 200);
    const auto all = pixel(over + "surface.paint(s, blend = surface.multiply, opacity = 1.0)");
    CHECK(all.r == multiply.r);
    const auto half = pixel(over + "surface.paint(s, blend = surface.source_over, opacity = 0.5)");
    CHECK(half.r == (100 * 128 * 255 + 200 * 255 * 127 + 32512) / 65025); // source alpha 128
    // A mask paints with its coverage, then the opacity.
    const auto masked = pixel(over + "surface.paint(mask.from(s), #6496c8, blend = surface.multiply, opacity = 1.0)");
    CHECK(masked.r == multiply.r);
}

TEST_CASE("blend and opacity are checked where they are written", "[textures]") {
    rejectSource("asset t{size 1x1 draw{layer s{} surface.paint(s, blend = 4)}}", "surface.paint blend must be surface.legacy, surface.source_over, surface.multiply");
    rejectSource("asset t{size 1x1 draw{layer s{} mode = 1 surface.paint(s, blend = mode)}}", "surface.paint blend must be");
    rejectSource("asset t{size 1x1 draw{layer s{} surface.paint(s, opacity = #fff)}}", "argument 2 of 'surface.paint' must be f32, got color");
    CHECK(trap("asset t{size 1x1 draw{layer s{} surface.paint(s, opacity = 1.5)}}") == "paint opacity must be in 0..1");
    CHECK(trap("asset t{size 1x1 draw{layer s{} surface.paint(s, opacity = 0.0 - 0.25)}}") == "paint opacity must be in 0..1");
    // A number in the place of the blend is an opacity; names say it outright.
    CHECK(generate("asset t{size 1x1 draw{layer s{pixel(0, 0, #fff)} surface.paint(s, 1.0)}}").result.image.pixels[0].a == 255);
    CHECK(generate("asset t{size 1x1 draw{layer s{pixel(0, 0, #fff)} surface.paint(s, surface.source_over, 1.0)}}").result.image.pixels[0].a == 255);
    CHECK(generate("asset t{size 1x1 draw{layer s{pixel(0, 0, #fff)} surface.paint(s, opacity = 1.0, blend = surface.add)}}").result.image.pixels[0].a == 255);
}

TEST_CASE("gradient maps brightness to colours and levels remaps channels", "[textures]") {
    const auto row = [](const std::string& filter) {
        return generate("asset t{size 4x1 draw{layer s(alpha = surface.source_over){pixel(0, 0, #000000) "
            "pixel(1, 0, #808080) pixel(2, 0, #ffffff) pixel(3, 0, #ffffff40)} "
            "layer r(alpha = surface.source_over){surface.paint(s" + filter + ")} "
            "surface.paint(r, blend = surface.source_over)}}").result.image.pixels;
    };
    const auto ramp = row(".gradient([#200000, #00ff00, #0000ffc0])");
    CHECK(ramp[0].r == 0x20);
    CHECK(ramp[0].a == 255);
    // Brightness 128 of 255 over three stops: 1/255 of the way into the second span.
    CHECK(ramp[1].g == (255 * 254 + 0 * 1 + 127) / 255);
    CHECK(ramp[2].b == 255);
    CHECK(ramp[2].a == 0xc0);
    // The stop's alpha times the source's.
    CHECK(ramp[3].a == (0xc0 * 0x40 + 127) / 255);
    const auto levelled = row(".levels(0.25, 0.75, 1.0)");
    CHECK(levelled[0].r == 0);
    CHECK(levelled[1].r == 129); // (128/255 - 0.25) / 0.5, to the nearest byte
    CHECK(levelled[2].r == 255);
    CHECK(levelled[3].a == 0x40);
    // black above white inverts, and gamma bends the middle.
    CHECK(row(".levels(1.0, 0.0, 1.0)")[0].r == 255);
    CHECK(row(".levels(0.0, 1.0, 2.0)")[1].r > 128);
    // A transparent pixel holds no colour after either filter.
    const auto hidden = generate("asset t{size 2x1 output roughness finish draw{layer s(alpha = surface.source_over)"
        "{pixel(0, 0, #ffffff)} finish = s.levels(1.0, 0.0, 1.0)}}");
    REQUIRE(hidden.result.images.size() == 1);
    CHECK(hidden.result.images[0].image.pixels[1].r == 255); // the map's background, not an inverted nothing
    // Arguments are named or positional, and integers widen.
    CHECK(row(".levels(gamma = 1, white = 1, black = 0)")[1].r == 128);
}

TEST_CASE("gradient and levels are checked when they are written and when they run", "[textures]") {
    rejectSource("asset t{size 1x1 draw{layer s{} g = s.gradient(#fff)}}", "colors must be");
    rejectSource("asset t{size 1x1 draw{layer s{} g = s.levels(0.0, 1.0)}}", "'.levels' is missing argument 'gamma'");
    rejectSource("asset t{size 1x1 draw{layer s{} m = mask.from(s) g = m.levels(0.0, 1.0, 1.0)}}", ".levels needs a layer receiver");
    CHECK(trap("asset t{size 1x1 draw{layer s{} g = s.gradient([#fff])}}") == "gradient needs 2..256 color stops");
    CHECK(trap("asset t{size 1x1 draw{layer s{} g = s.levels(0.5, 0.5, 1.0)}}") ==
          "levels needs distinct black and white in 0..1 and finite positive gamma");
    CHECK(trap("asset t{size 1x1 draw{layer s{} g = s.levels(0.0, 1.0, 0.0)}}") ==
          "levels needs distinct black and white in 0..1 and finite positive gamma");
    CHECK(trap("asset t{size 1x1 draw{layer s{} g = s.levels(0.0, 1.5, 1.0)}}") ==
          "levels needs distinct black and white in 0..1 and finite positive gamma");
    CHECK(trap("asset t{size 1x1 draw{layer n(encoding = surface.normal){} g = n.levels(0.0, 1.0, 1.0)}}") ==
          "levels needs a colour layer, not a direction layer");
    CHECK(trap("asset t{size 1x1 draw{layer n(encoding = surface.normal){} g = n.gradient([#000, #fff])}}") ==
          "gradient needs a colour layer, not a direction layer");
}

TEST_CASE("a material named gradient or levels compiles as it did in 1.1", "[textures]") {
    // `mats.gradient()` is a call on an import alias: a material, never the
    // method of a layer. The bytes are those of the same library renamed.
    const auto compiled = [](const std::string& first, const std::string& second) {
        SourceCompilationOptions options;
        const auto result = compileSource("root.ips", [&](std::string_view path) -> std::string {
            if (path == "lib.ips") return "material " + first + " { base = #808080 }\nmaterial " + second + " { base = #a06040 }\n";
            return "import mats from \"lib.ips\"\nasset a { size 2x1 draw { g = mats." + first + "() l = mats." + second +
                   "() pixel(0, 0, g.base) pixel(1, 0, l.base) } }\n";
        }, options);
        REQUIRE(result.succeeded());
        return result.bytes;
    };
    const auto image = generate(compiled("gradient", "levels")).result.image;
    CHECK(image.pixels[0].r == 0x80);
    CHECK(image.pixels[1].r == 0xa0);
    CHECK(compiled("gradient", "levels") == compiled("Gradient", "Levels"));
}

TEST_CASE("a periodic noise repeats exactly over its period", "[textures]") {
    // Coordinates are binary fractions, so a shift by the period is exact
    // and the two samples must agree in every bit.
    const std::vector<std::pair<const char*, const char*>> calls{
        {"value", "noise.value(X, Y, s, period = {6, 4})"},
        {"grad", "noise.grad(X, Y, s, period = {6, 4})"},
        {"fbm", "noise.fbm(X, Y, 5, s, period = {6, 4})"},
        {"fbm with lacunarity and gain", "noise.fbm(X, Y, 4, 2.5, 0.6, s, period = {6, 4})"},
        {"ridge", "noise.ridge(X, Y, 4, s, period = {6, 4})"},
        {"cell", "noise.cell(X, Y, s, period = {6, 4})"},
        {"cell2", "noise.cell2(X, Y, s, period = {6, 4})"},
        {"cell_id", "float(noise.cell_id(X, Y, s, period = {6, 4}) & 65535)"},
        {"warp", "noise.warp(X, Y, 0.35, s, period = {6, 4})"},
        {"a period of one cell", "noise.grad(X, Y, s, period = {1, 1})"},
    };
    for (const auto& [name, call] : calls) {
        INFO(name);
        const std::string here = replaced(replaced(call, "X", "x"), "Y", "y");
        const std::string there = replaced(replaced(call, "X", "(x + 6.0)"), "Y", "(y - 8.0)");
        const auto run = generate("asset t{size 1x1 output list point pairs draw{s = seed.initial().fork(\"n\") "
            "for i in 0..48 { x = float(i) * 0.171875 - 3.0 y = float(i % 7) * 0.59375 + 0.25 pairs = {" + here + ", " + there + "} }}}");
        const auto* pairs = run.result.find("pairs");
        REQUIRE(pairs != nullptr);
        REQUIRE(pairs->points.size() == 48);
        bool varies = false;
        for (const auto& point : pairs->points) {
            CHECK(std::bit_cast<u32>(point.x) == std::bit_cast<u32>(point.y));
            varies = varies || point.x != pairs->points[0].x;
        }
        CHECK(varies);
    }
    // warp_vec returns the moved point, so its displacement is what repeats.
    const auto run = generate("asset t{size 1x1 output list point moved draw{s = seed.initial().fork(\"n\") "
        "for i in 0..24 { x = float(i) * 0.171875 y = float(i % 5) * 0.59375 "
        "a = noise.warp_vec(x, y, 0.35, s, period = {6, 4}) b = noise.warp_vec(x + 6.0, y + 4.0, 0.35, s, period = {6, 4}) "
        "moved = {a.x - x, b.x - (x + 6.0)} moved = {a.y - y, b.y - (y + 4.0)} }}}");
    for (const auto& point : run.result.find("moved")->points) CHECK(std::fabs(point.x - point.y) < 1e-4f);
    // Without a period every noise is what it was.
    const auto plain = generate("asset t{size 1x1 output point p draw{p = {noise.fbm(1.25, 2.5, 4, 9), noise.cell(1.25, 2.5, 9)}}}");
    const auto wide = generate("asset t{size 1x1 output point p draw{p = {noise.fbm(1.25, 2.5, 4, 9, period = {4096, 4096}), "
                               "noise.cell(1.25, 2.5, 9, period = {4096, 4096})}}}");
    CHECK(std::fabs(plain.result.find("p")->points[0].x - wide.result.find("p")->points[0].x) < 1e-5f);
    CHECK(std::fabs(plain.result.find("p")->points[0].y - wide.result.find("p")->points[0].y) < 1e-5f);
}

TEST_CASE("periods and tile flags are checked", "[textures]") {
    const auto sample = [](const std::string& period) {
        return trap("asset t{size 1x1 output point p draw{p = {noise.grad(0.5, 0.5, 3, period = " + period + "), 0}}}");
    };
    CHECK(sample("{0, 4}") == "noise period must contain positive i32 integers");
    CHECK(sample("{4.5, 4}") == "noise period must contain positive i32 integers");
    CHECK(sample("{4, 0.0 - 2.0}") == "noise period must contain positive i32 integers");
    CHECK(sample("{4, 4}") == "no trap");
    rejectSource("asset t{size 1x1 draw{m = noise.mask({0, 0}, {1, 1}, noise.mode_fbm, {0.1, 0.1}, 0.0, 3, tile = 2)}}",
                 "noise.mask tile must be true or false");
    // Periodic noise is a tool of draw assets: a render asset has no texture page.
    SourceCompilationOptions options;
    const auto model = compileSource("root.ips", [](std::string_view) -> std::string {
        return "asset m { size 4x4x4 scale 100 render { x = noise.grad(0.5, 0.5, 3, period = {4, 4}) } }\n";
    }, options);
    REQUIRE_FALSE(model.succeeded());
    CHECK_THAT(model.diagnostics.front().message, ContainsSubstring("the periodic overload of noise.grad requires an asset with draw"));
}

TEST_CASE("a tiled noise mask meets itself at the edges and is a tiled mask", "[textures]") {
    // The step from the last column to the first is as small as a step
    // between neighbours; without tiling it is a jump.
    const auto seam = [](const std::string& tile) {
        const auto image = generate("asset t{size 64x64 draw{m = noise.mask({0, 0}, {64, 64}, noise.mode_fbm, {0.07, 0.07}, 0.0, "
            "seed.initial().fork(\"m\"), octaves = 3, softness = 1.0, comparison = noise.above, warp_amount = 0.3" + tile + ") "
            "layer s(alpha = surface.source_over){surface.paint(m, #ffffff)} surface.paint(s, blend = surface.source_over)}}").result.image;
        u64 across = 0, inside = 0;
        for (u32 y = 0; y < 64; ++y) {
            const auto alpha = [&](u32 x) { return static_cast<int>(image.pixels[static_cast<std::size_t>(y) * 64 + x].a); };
            across += static_cast<u64>(std::abs(alpha(63) - alpha(0)));
            for (u32 x = 0; x + 1 < 64; ++x) inside += static_cast<u64>(std::abs(alpha(x + 1) - alpha(x)));
        }
        return std::pair<double, double>{static_cast<double>(across) / 64.0, static_cast<double>(inside) / (64.0 * 63.0)};
    };
    const auto tiled = seam(", tile = true"), plain = seam("");
    CHECK(tiled.first < 2.5 * tiled.second);
    CHECK(plain.first > 4.0 * plain.second);
    // Over exactly the canvas it is one period, so growing it wraps: a mask
    // that touches the left edge grows into the right one.
    const auto grown = [](const std::string& tile) {
        return covered(generate("asset t{size 32x32 draw{m = noise.mask({0, 0}, {32, 32}, noise.mode_cell, {0.1, 0.1}, 0.35, "
            "seed.initial().fork(\"m\"), octaves = 1, softness = 0.0, comparison = noise.below, warp_amount = 0.0" + tile + ") "
            "surface.paint(mask.expand(m, 3), #ffffff)}}").result.image);
    };
    CHECK(grown(", tile = true") > 0);
    CHECK(grown(", tile = false") == grown(""));
}

TEST_CASE("the texture page costs what its operations cost elsewhere", "[textures]") {
    const auto spent = [](const std::string& body) { return generate("asset t{size 64x32 draw{" + body + "}}", false).fuel; };
    const std::string scene = "layer s(alpha = surface.source_over){rect(0, 0, 64, 32, #804020)} ";
    const u64 base = spent(scene);
    // A plain paint costs what it always did, ceil((1 + W*H) * 60 / 100),
    // after one instruction for its layer. One that blends with a mode of the
    // toolkit or fades pays whole units, 1 + W*H, and one more instruction
    // for its opacity.
    const u64 plain = (static_cast<u64>(1 + 64 * 32) * 60 + 99) / 100;
    const u64 whole = 1 + 64 * 32;
    CHECK(spent(scene + "surface.paint(s)") - base == plain + 1);
    CHECK(spent(scene + "surface.paint(s, blend = surface.source_over)") - base == plain + 1);
    CHECK(spent(scene + "surface.paint(s, blend = surface.multiply)") - base == whole + 1);
    CHECK(spent(scene + "surface.paint(s, opacity = 0.5)") - base == whole + 2);
    CHECK(spent(scene + "surface.paint(s, blend = surface.multiply, opacity = 0.5)") - base == whole + 2);
    // The same for a mask, whose units are the pixels of its bounds. The
    // control stores the mask where the paint pushes its colour.
    const u64 masked = spent(scene + "m = mask.from(s)");
    CHECK(spent(scene + "surface.paint(mask.from(s), #fff)") - masked == plain);
    CHECK(spent(scene + "surface.paint(mask.from(s), #fff, blend = surface.add)") - masked == whole);
    CHECK(spent(scene + "surface.paint(mask.from(s), #fff, blend = surface.add, opacity = 0.5)") - masked == whole + 1);
    // gradient and levels: the table and a pixel each at half a unit.
    const u64 filter = (static_cast<u64>(1 + 256 + 64 * 32) * 50 + 99) / 100;
    CHECK(spent(scene + "g = s.levels(0.0, 1.0, 1.0)") - spent(scene + "g = s") == filter + 3);
    // A square around the corner of a tiled capture is four squares.
    const u64 one = spent("layer a{rect(8, 8, 6, 6, #fff)}") - spent("layer a{}");
    const u64 wrapped = spent("layer a(tile = true){rect(-3, -3, 6, 6, #fff)}") - spent("layer a(tile = true){}");
    // Three copies push no arguments again: four numbers, their conversions
    // and the colour, nine units a square.
    CHECK(one == 9 + 1 + 7 * 7);
    CHECK(wrapped == 4 * one - 3 * 9);
    // Periodic scalar noise pays its kernel units, the dispatch among them.
    const auto call = [&](const std::string& expression) {
        return spent("x = " + expression) - spent("x = 0.5");
    };
    const u64 period = call("noise.grad(0.5, 0.5, 3, period = {4, 4})") - call("noise.grad(0.5, 0.5, 3)");
    // Three more units for the kernel, and five instructions for the period:
    // two numbers, their conversions and the vector.
    CHECK(period == 3 + 5);
    CHECK(call("noise.fbm(0.5, 0.5, 5, 3, period = {4, 4})") - call("noise.fbm(0.5, 0.5, 5, 3)") == (5 * 4 - 1) + 5);
    CHECK(call("noise.cell(0.5, 0.5, 3, period = {4, 4})") - call("noise.cell(0.5, 0.5, 3)") == 24 + 5);
    CHECK(call("noise.cell2(0.5, 0.5, 3, period = {4, 4})") - call("noise.cell2(0.5, 0.5, 3)") == 5);
}

TEST_CASE("a texture program survives the assembler and the old page is still closed", "[textures]") {
    const auto bytes = compileChecked("asset page{size 16x16 draw{root = seed.initial() "
        "layer base(tile = true, alpha = surface.source_over){rect(0, 0, 16, 16, #808080) rect(-2, 3, 4, 4, #c0c0c0)} "
        "spots = noise.mask({0, 0}, {16, 16}, noise.mode_fbm, {0.2, 0.2}, 0.1, root.fork(\"spots\"), tile = true) "
        "n = noise.fbm(1.5, 2.5, 4, root.fork(\"n\"), period = {4, 4}) "
        "tinted = base.gradient([#201008, #c08040, #fff0d0]).levels(0.1, 0.9, 1.2) "
        "surface.paint(tinted, blend = surface.source_over) surface.paint(base, blend = surface.multiply, opacity = 0.5) "
        "surface.paint(spots, color.shade(#804020, int(n * 20.0)), blend = surface.overlay, opacity = 0.75)}}", false);
    const auto text = disassemble(bytes);
    for (const char* line : {"texture noise.mask 0", "texture noise.fbm 0", "texture gradient 0", "texture levels 0",
                             "texture paint 4", "texture paint.mask 6", "layer.begin"})
        CHECK_THAT(text, ContainsSubstring(line));
    const auto again = assemble(text);
    CHECK(disassemble(again) == text);
    CHECK(same(generate(again).result.image.pixels, generate(bytes).result.image.pixels));
    CHECK_THROWS_WITH(assemble(replaced(text, "texture gradient 0", "texture polish 0")),
                      ContainsSubstring("texture expects paint, paint.mask, gradient, levels, a periodic noise or a voronoi mnemonic"));
}

// ---- tiled diagrams (M5) ----------------------------------------------------

namespace {
// Every cell of a diagram added once, as grey 127, over black in a tiled
// capture: a pixel is 127 where exactly one cell covers it, through an edge
// too, about 0 in a hole and about 254 under two.
std::string cells(const std::string& diagram, const std::string& regions = "regions(d)") {
    return "asset t{size 64x48 output point total draw{root = seed.initial() d = " + diagram + " rs = " + regions +
        " layer img(tile = true){rect(0, 0, 64, 48, #000000) "
        "for i in 0..regions.count(rs) { surface.paint(regions.mask(rs, i), #7f7f7f, blend = surface.add) }} "
        "surface.paint(img) area = 0.0 for i in 0..regions.count(rs) { area = area + regions.area(rs, i) } "
        "total = {area, float(regions.count(rs))}}}";
}
struct Coverage { u32 lowest = 255, highest = 0; f32 area = 0, count = 0; };
Coverage coverage(const std::string& source) {
    const auto run = generate(source);
    Coverage result;
    for (const auto& pixel : run.result.image.pixels) {
        result.lowest = std::min<u32>(result.lowest, pixel.r);
        result.highest = std::max<u32>(result.highest, pixel.r);
    }
    const auto& total = run.result.outputs.at(0).points.at(0);
    result.area = total.x;
    result.count = total.y;
    return result;
}
} // namespace

TEST_CASE("the cells of a tiled diagram cover their rectangle once, through its edges", "[textures][voronoi]") {
    for (const char* diagram : {
             "voronoi({0, 0}, {64, 48}, 20, root.fork(\"a\"), tile = true)",
             "voronoi({0, 0}, {64, 48}, 28, root.fork(\"b\"), voronoi.grid, 0.8, 2, tile = true)",
             "voronoi({0, 0}, {64, 48}, 1, root.fork(\"c\"), tile = true)",
             "voronoi({0, 0}, {64, 48}, 12, root.fork(\"d\"), voronoi.random, 1.0, 1, 2.0, {1, 1}, tile = true)",
             "voronoi({0, 0}, {64, 48}, [{5, 6}, {40, 9}, {22, 30}, {58, 41}, {9, 44}], 1, tile = true)"}) {
        INFO(diagram);
        const auto tiled = coverage(cells(diagram));
        CHECK(tiled.lowest >= 124);
        CHECK(tiled.highest <= 130);
        CHECK(std::abs(tiled.area - 64.f * 48.f) < 0.05f);
        // The same diagram without the flag stops at the edges: it covers
        // its rectangle too, with other cells.
        const auto plain = coverage(cells(replaced(diagram, ", tile = true", "")));
        CHECK(plain.lowest >= 124);
        CHECK(plain.highest <= 130);
        CHECK(std::abs(plain.area - 64.f * 48.f) < 0.05f);
        CHECK(plain.count == tiled.count);
    }
    // A gap of the regions is the same on both sides of an edge: no cell is
    // cut there, so every pixel is in a cell or in the gap between two.
    const std::string gaps = cells("voronoi({0, 0}, {64, 48}, 16, root.fork(\"e\"), tile = true)",
                                   "regions.gap(regions(d), 1.5, 0.0, 5)");
    const auto gapped = generate(gaps);
    std::size_t solid = 0;
    for (const auto& pixel : gapped.result.image.pixels) solid += pixel.r >= 124;
    CHECK(solid > gapped.result.image.pixels.size() / 2);
    CHECK(solid < gapped.result.image.pixels.size());
    const auto shrunk = coverage(gaps);
    CHECK(shrunk.lowest == 0);     // the gap itself
    CHECK(shrunk.highest <= 130);  // and no cell over another, at an edge either
    CHECK(shrunk.area < 64.f * 48.f);
}

TEST_CASE("a tiled diagram counts neighbours through its edges and has its own limits", "[textures][voronoi]") {
    // Three sites in a row: the outer two meet through the left and right edges.
    const auto neighbours = [](const std::string& flag) {
        const auto run = generate("asset t{size 64x64 output point n draw{"
            "d = voronoi({0, 0}, {64, 64}, [{8, 32}, {32, 30}, {56, 34}]" + flag + ") "
            "n = {float(list.len(diagram.neighbors(d, 0))), float(list.len(diagram.neighbors(d, 1)))}}}");
        const auto& point = run.result.outputs.at(0).points.at(0);
        return Vec2{point.x, point.y};
    };
    CHECK(neighbours("").x == 1);
    CHECK(neighbours("").y == 2);
    CHECK(neighbours(", tile = true").x == 2);
    CHECK(neighbours(", tile = true").y == 2);
    // The flag off is the plain diagram, by the page.
    const std::string plain = "asset t{size 32x32 draw{d = voronoi({0, 0}, {32, 32}, 9, 3FLAG) rs = regions(d) "
        "for i in 0..regions.count(rs) { surface.paint(regions.mask(rs, i), color.shade(#808080, i * 9 - 40)) }}}";
    const auto without = generate(replaced(plain, "FLAG", "")), off = generate(replaced(plain, "FLAG", ", tile = false"));
    CHECK(same(without.result.image.pixels, off.result.image.pixels));
    CHECK(off.fuel == without.fuel + 1); // the flag
    // Nine times the sites: the diagram costs several times the plain one.
    CHECK(generate(replaced(plain, "FLAG", ", tile = true")).fuel > 2 * without.fuel);

    const auto count = [](int cells) {
        return trap("asset t{size 8x8 draw{d = voronoi({0, 0}, {8, 8}, " + std::to_string(cells) + ", 3, tile = true)}}");
    };
    CHECK(count(28) == "no trap");
    CHECK(count(29) == "voronoi count must be in 1..28 with tile");
    CHECK(trap("asset t{size 8x8 draw{d = voronoi({0, 0}, {8, 8}, 29, 3, tile = false)}}") == "no trap");
    // A site on the right edge is the one on the left edge.
    CHECK(trap("asset t{size 8x8 draw{d = voronoi({0, 0}, {8, 8}, [{0, 3}, {8, 3}], tile = true)}}") ==
          "voronoi sites must be distinct");
    rejectSource("asset t{size 8x8 draw{d = voronoi({0, 0}, {8, 8}, 4, 3, tile = 2)}}", "voronoi tile must be true or false");

    const auto bytes = compileChecked("asset page{size 16x16 draw{"
        "a = voronoi({0, 0}, {16, 16}, 6, 3, voronoi.grid, 0.5, 1, tile = true) "
        "b = voronoi({0, 0}, {16, 16}, [{2, 3}, {9, 12}], tile = true)}}", false);
    const auto text = disassemble(bytes);
    CHECK_THAT(text, ContainsSubstring("texture voronoi.config 0"));
    CHECK_THAT(text, ContainsSubstring("texture voronoi.sites 0"));
    CHECK(disassemble(assemble(text)) == text);
}
