#include <catch2/catch_test_macros.hpp>
#include "SourceCompilation.hpp"
#include "iprop/Compression.hpp"
#include "iprop/Debug.hpp"
#include "iprop/NormalMap.hpp"
#include "iprop/VM.hpp"
#include "ips/BuiltinSpec.hpp"
#include "ips/Parser.hpp"
#include "ips/Printer.hpp"
#include "ipa.hpp"
#include "ipdis.hpp"
#include "support/compiler_support.hpp"

#include <array>
#include <cmath>
#include <limits>
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

GenerationResult produce(const std::vector<u8>& bytes, const std::map<std::string, f32>& inputs = {},
                         u32 width = 0, u32 height = 0) {
    const auto file = loadFile(bytes); validate(file);
    GenerationInputs request; request.seed = 26; request.sizeX = width; request.sizeY = height;
    for (const auto& [name, value] : inputs) {
        UniformValue uniform; uniform.type = InputType::F32; uniform.f = value;
        request.uniforms.emplace(name, uniform);
    }
    VM vm(file, request); vm.run();
    return vm.takeResult();
}
const Canvas& normalOf(const GenerationResult& result) {
    REQUIRE(result.images.size() == 1);
    REQUIRE(result.images[0].kind == AuxImageKind::Normal);
    REQUIRE(result.images[0].source == AuxImageSource::Authored);
    return result.images[0].image;
}
std::string trapOf(const std::vector<u8>& bytes, const std::map<std::string, f32>& inputs = {}) {
    try { (void)produce(bytes, inputs); }
    catch (const Trap& error) { return error.what(); }
    return {};
}

std::string transform(u8 kind) {
    constexpr std::array rotations{0, 90, 180, 270, 0, 0, 90, 90};
    return ",rot=" + std::to_string(rotations[kind]) + ",mirror_x=" + std::to_string(kind == 4 || kind == 7) +
        ",mirror_y=" + std::to_string(kind == 5 || kind == 6);
}
// The pixel mapping of spec §10.5, written out instead of calling D4Transform.
RasterPoint moved(u8 kind, RasterPoint p, RasterPoint extent) {
    switch (kind) {
    case 0: return p; case 1: return {p.y, extent.x - 1 - p.x};
    case 2: return {extent.x - 1 - p.x, extent.y - 1 - p.y};
    case 3: return {extent.y - 1 - p.y, p.x}; case 4: return {extent.x - 1 - p.x, p.y};
    case 5: return {p.x, extent.y - 1 - p.y}; case 6: return {p.y, p.x};
    default: return {extent.y - 1 - p.y, extent.x - 1 - p.x};
    }
}
// The byte table of the normal-map contract, written out as well.
Color turned(u8 kind, Color c) {
    const auto n = [](u8 v) { return static_cast<u8>(255 - v); };
    switch (kind) {
    case 0: return c; case 1: return {n(c.g), c.r, c.b, c.a};
    case 2: return {n(c.r), n(c.g), c.b, c.a}; case 3: return {c.g, n(c.r), c.b, c.a};
    case 4: return {n(c.r), c.g, c.b, c.a}; case 5: return {c.r, n(c.g), c.b, c.a};
    case 6: return {n(c.g), n(c.r), c.b, c.a}; default: return {c.g, c.r, c.b, c.a};
    }
}

// A 5x3 part: an opaque direction layer made of a height ramp that differs on
// every side and one off-axis painted facet.
const std::string kLeaf =
    "asset leaf{size 5x3 output normal relief draw{"
    "layer shape(alpha=surface.source_over){rect(0,0,5,3,#ffffff40)rect(1,0,3,2,#ffffff90)rect(3,1,2,2,#fffffff0)}"
    "layer n(encoding=surface.normal){rect(0,0,5,3,color.normal(0,0,1))"
    "surface.paint(mask.from(shape).normal(3.0))"
    "pixel(4,0,color.normal(-0.6,0.3,0.7))}"
    "relief=n}}";

u8 storedByte(f32 n) {
    const f32 scaled = 128.f + 127.5f * std::fabs(n);
    const auto stored = static_cast<int>(std::floor(std::fmin(scaled, 255.f)));
    return static_cast<u8>(n < 0.f ? 255 - stored : stored);
}
// color.normal written from its contract with plain f32 arithmetic.
Color directionColour(f32 x, f32 y, f32 z) {
    const f32 d = (x * x + y * y) + z * z;
    if (!(d > 0.f) || !std::isfinite(d)) return {128, 128, 255, 255};
    const f32 length = std::sqrt(d);
    return {storedByte(x / length), storedByte(-(y / length)), storedByte(z / length), 255};
}

} // namespace

TEST_CASE("a part's directions follow every D4 placement exactly, also through a nested part", "[relief]") {
    const auto alone = produce(compileChecked(kLeaf));
    const Canvas& leaf = normalOf(alone);
    REQUIRE(leaf.width == 5); REQUIRE(leaf.height == 3);
    // The part is asymmetric: no two transforms can be confused.
    CHECK(leaf.at(4, 0) == directionColour(-0.6f, 0.3f, 0.7f));
    CHECK(leaf.at(0, 0) != leaf.at(4, 2)); CHECK(leaf.at(1, 0) != leaf.at(1, 2)); CHECK(leaf.at(0, 1) != leaf.at(4, 1));
    const RasterPoint extent{5, 3};

    for (const bool optimize : {false, true}) for (u8 kind = 0; kind < 8; ++kind) {
        CAPTURE(optimize, kind);
        const auto bytes = compileFiles({
            {"root.ips", "import leaf from \"leaf.ips\" asset t{size 12x12 output normal relief draw{"
                         "leaf.draw(seed,at={4,5}" + transform(kind) + ")relief=leaf.outputs.relief}}"},
            {"leaf.ips", kLeaf}}, optimize);
        const auto result = produce(bytes);
        std::vector<Color> expected(144, kFlatDirection);
        for (i32 y = 0; y < 3; ++y) for (i32 x = 0; x < 5; ++x) {
            const auto at = moved(kind, {x, y}, extent);
            expected[static_cast<std::size_t>(5 + at.y) * 12 + static_cast<std::size_t>(4 + at.x)] =
                turned(kind, leaf.at(static_cast<u32>(x), static_cast<u32>(y)));
        }
        CHECK(normalOf(result).pixels == expected);
        // Only the root's output is published, under the root's name.
        CHECK(result.images[0].name == "relief");
    }

    // A middle part re-exports the leaf's layer; the root places the middle.
    for (u8 inner = 0; inner < 8; ++inner) for (u8 outer = 0; outer < 8; ++outer) {
        CAPTURE(inner, outer);
        const auto bytes = compileFiles({
            {"root.ips", "import middle from \"middle.ips\" asset t{size 24x24 output normal relief draw{"
                         "middle.draw(seed,at={9,8}" + transform(outer) + ")relief=middle.outputs.relief}}"},
            {"middle.ips", "import leaf from \"leaf.ips\" asset middle{size 7x6 output normal relief draw{"
                           "leaf.draw(seed,at={1,2}" + transform(inner) + ")relief=leaf.outputs.relief}}"},
            {"leaf.ips", kLeaf}});
        std::vector<Color> expected(576, kFlatDirection);
        for (i32 y = 0; y < 3; ++y) for (i32 x = 0; x < 5; ++x) {
            const auto inMiddle = moved(inner, {x, y}, extent);
            const auto at = moved(outer, {1 + inMiddle.x, 2 + inMiddle.y}, {7, 6});
            expected[static_cast<std::size_t>(8 + at.y) * 24 + static_cast<std::size_t>(9 + at.x)] =
                turned(outer, turned(inner, leaf.at(static_cast<u32>(x), static_cast<u32>(y))));
        }
        CHECK(normalOf(produce(bytes)).pixels == expected);
    }
}

TEST_CASE("a parent paints several parts' directions into its own capture", "[relief]") {
    const auto alone = produce(compileChecked(kLeaf));
    const Canvas& leaf = normalOf(alone);
    const auto bytes = compileFiles({
        {"root.ips", "import leaf from \"leaf.ips\" asset t{size 16x8 output normal relief draw{"
                     "leaf.draw(seed,at={1,1})a=leaf.outputs.relief "
                     "leaf.draw(seed,at={9,2},mirror_x=1)b=leaf.outputs.relief "
                     "layer n(encoding=surface.normal){surface.paint(a)surface.paint(b)}relief=n}}"},
        {"leaf.ips", kLeaf}});
    const auto result = produce(bytes);
    const Canvas& image = normalOf(result);
    for (u32 y = 0; y < 3; ++y) for (u32 x = 0; x < 5; ++x) {
        CHECK(image.at(1 + x, 1 + y) == leaf.at(x, y));
        CHECK(image.at(9 + (4 - x), 2 + y) == turned(4, leaf.at(x, y)));
    }
    CHECK(image.at(0, 0) == kFlatDirection); CHECK(image.at(15, 7) == kFlatDirection);
}

TEST_CASE("the normal method converts a mask or a height layer, in every spelling", "[relief]") {
    const std::string head = "asset t{size 10x10 output normal relief draw{layer body{oval(5,5,3,3,#fff)}";
    const auto full = normalOf(produce(compileChecked(head + "relief=mask.from(body).normal(5.0,1.0)}}"))).pixels;
    for (const std::string call : {"mask.from(body).normal(5,1)", "mask.from(body).normal(depth=5.0,smoothing=1.0)",
            "mask.from(body).normal(5.0,smoothing=1)", "m=mask.from(body) relief=m.normal(5.0,1.0) x=mask.from(body).normal(5.0,1.0)"}) {
        CAPTURE(call);
        const auto source = call.find("relief=") == std::string::npos ? head + "relief=" + call + "}}" : head + call + "}}";
        for (const bool optimize : {false, true}) CHECK(normalOf(produce(compileChecked(source, optimize))).pixels == full);
    }
    // The short form passes smoothing zero.
    const auto plain = normalOf(produce(compileChecked(head + "relief=mask.from(body).normal(5.0)}}"))).pixels;
    CHECK(plain == normalOf(produce(compileChecked(head + "relief=mask.from(body).normal(5.0,0.0)}}"))).pixels);
    CHECK(plain != full);
    // An opaque white layer is the same height as its own alpha.
    CHECK(normalOf(produce(compileChecked(head + "relief=body.normal(5.0)}}"))).pixels == plain);
    // Outside the shape the result has no coverage: the image stays flat. The
    // rim of the disc faces outward, and the disc is symmetric.
    const auto image = normalOf(produce(compileChecked(head + "relief=mask.from(body).normal(5.0)}}")));
    CHECK(image.at(0, 0) == kFlatDirection);
    CHECK(image.at(2, 5).r < 128); CHECK(image.at(8, 5).r > 128);
    CHECK(image.at(5, 2).g > 128); CHECK(image.at(5, 8).g < 128);
    CHECK(image.at(2, 5).r + image.at(8, 5).r == 255); CHECK(image.at(5, 2).g + image.at(5, 8).g == 255);
    // A shape cut by the canvas edge has no slope there: heights repeat outward.
    const auto cut = normalOf(produce(compileChecked("asset t{size 4x4 output normal relief draw{layer body{rect(0,0,4,4,#fff)}"
        "relief=mask.from(body).normal(5.0)}}")));
    CHECK(cut.pixels == std::vector<Color>(16, kFlatDirection));
    // A helper takes and returns direction layers like any layer.
    const auto helped = compileChecked("asset t{size 10x10 output normal relief "
        "bump(shape:mask,d:f32)->layer{return shape.normal(d)} "
        "draw{layer body{oval(5,5,3,3,#fff)}relief=bump(mask.from(body),5.0)}}");
    CHECK(normalOf(produce(helped)).pixels == plain);
    // Depth and smoothing are in raster pixels, like blur's sigma: at twice
    // the size a ramp is half as steep per pixel, so its directions are flatter.
    const auto ramp = "asset t{size 4x1 output normal relief draw{layer h(alpha=surface.source_over){"
        "rect(0,0,1,1,#ffffff00)rect(1,0,1,1,#ffffff40)rect(2,0,1,1,#ffffff80)rect(3,0,1,1,#ffffffc0)}relief=mask.from(h).normal(2.0)}}";
    const auto small = produce(compileChecked(ramp)), large = produce(compileChecked(ramp), {}, 8, 2);
    CHECK(normalOf(large).width == 8); CHECK(normalOf(large).height == 2);
    CHECK(normalOf(small).at(1, 0).r < normalOf(large).at(3, 0).r);
    CHECK(normalOf(large).at(3, 0).r < 128);
}

TEST_CASE("color.normal stores a direction, folds constants and never traps", "[relief]") {
    const std::string dynamic = "asset t{size 1x1 input f32 x=0 input f32 y=0 input f32 z=1 output normal relief draw{"
        "layer n(encoding=surface.normal){pixel(0,0,color.normal(x,y,z))}relief=n}}";
    const auto infinity = std::numeric_limits<f32>::infinity(), nan = std::numeric_limits<f32>::quiet_NaN();
    const std::vector<std::array<f32, 3>> vectors{
        {0,0,1}, {1,0,0}, {-1,0,0}, {0,1,0}, {0,-1,0}, {0,0,-1}, {1,1,1}, {-1,1,1}, {1,-1,1}, {-0.6f,0.3f,0.7f},
        {0.6f,0.3f,0.7f}, {2,0,2.236068f}, {1e-20f,0,1e-20f}, {1e-30f,0,1e-30f}, {3e18f,0,3e18f}, {1e20f,0,1},
        {0,0,0}, {-0.f,0,0}, {infinity,0,1}, {nan,0,1}, {0,nan,1}, {0,0,nan}, {0.001f,-0.002f,5}, {7,-11,13}};
    for (const bool optimize : {false, true}) {
        const auto bytes = compileChecked(dynamic, optimize);
        for (const auto& v : vectors) {
            CAPTURE(optimize, v[0], v[1], v[2]);
            const auto expected = directionColour(v[0], v[1], v[2]);
            CHECK(normalOf(produce(bytes, {{"x", v[0]}, {"y", v[1]}, {"z", v[2]}})).at(0, 0) == expected);
            CHECK(ips::normalColorValue(v[0], v[1], v[2]).color == expected);
        }
    }
    // Hand-computed bytes: source y is downward, stored y upward.
    CHECK(directionColour(0, 0, 1) == Color{128, 128, 255, 255});
    CHECK(directionColour(1, 0, 0) == Color{255, 128, 128, 255});
    CHECK(directionColour(-1, 0, 0) == Color{0, 128, 128, 255});
    CHECK(directionColour(0, 1, 0) == Color{128, 0, 128, 255});
    CHECK(directionColour(0, -1, 0) == Color{128, 255, 128, 255});
    CHECK(directionColour(1, 1, 1) == Color{201, 54, 201, 255});
    CHECK(directionColour(0, 0, 0) == Color{128, 128, 255, 255});
    CHECK(ips::normalColorValue(0, 0, 0).flat);
    CHECK_FALSE(ips::normalColorValue(0, 0, 1).flat);
    // A component and its negation are exact complements.
    for (const auto& v : vectors) {
        if (!(v[0] != 0) || ips::normalColorValue(v[0], v[1], v[2]).flat) continue;
        CHECK(directionColour(v[0], v[1], v[2]).r + directionColour(-v[0], v[1], v[2]).r == 255);
    }

    // Constant arguments fold to one colour constant with the same bytes.
    for (const bool optimize : {false, true}) {
        const auto folded = compileChecked("asset t{size 1x1 const f32 tilt=-0.6 const color stored=color.normal(1,1,1) "
            "output normal relief draw{layer n(encoding=surface.normal){pixel(0,0,color.normal(tilt,0.3,0.7))}relief=n}}", optimize);
        const auto text = disassemble(stripDebug(folded));
        CHECK(text.find("f32.sqrt") == std::string::npos);
        CHECK(text.find("const.color #" ) != std::string::npos);
        CHECK(normalOf(produce(folded)).at(0, 0) == directionColour(-0.6f, 0.3f, 0.7f));
    }
    const auto constant = compileChecked("asset t{size 1x1 const color stored=color.normal(1,1,1) output normal relief draw{"
        "layer n(encoding=surface.normal){pixel(0,0,stored)}relief=n}}");
    CHECK(normalOf(produce(constant)).at(0, 0) == Color{201, 54, 201, 255});
    // Evaluation order: x, then y, then z, once each.
    const auto ordered = compileChecked("asset t{size 1x1 output normal relief output list point seen "
        "note(v:f32)->f32{seen={v,v} return v} draw{layer n(encoding=surface.normal){"
        "pixel(0,0,color.normal(z=note(3.0),x=note(1.0),y=note(2.0)))}relief=n}}");
    const auto order = produce(ordered);
    REQUIRE(order.outputs.size() == 1);
    REQUIRE(order.outputs[0].points.size() == 3);
    CHECK(order.outputs[0].points[0].x == 1.f); CHECK(order.outputs[0].points[1].x == 2.f); CHECK(order.outputs[0].points[2].x == 3.f);
    CHECK(normalOf(order).at(0, 0) == directionColour(1, 2, 3));
}

TEST_CASE("normal outputs captures and methods have located diagnostics", "[relief]") {
    const std::string body = "draw{layer n(encoding=surface.normal){}relief=n}}";
    rejectSource("asset t{size 1x1 output normal a output normal b draw{}}", "an asset declares at most one normal output");
    rejectSource("asset t{size 1x1 output list normal relief draw{}}", "resource exports cannot be lists");
    rejectSource("asset t{size 1x1 output relief draw{}}", "an output kind (point, rect, circle, mask, layer, normal, roughness, metallic, ao, emission or list part)");
    rejectSource("asset t{size 1x1 output normal relief draw{relief=#fff}}", "output 'relief' expects layer, got color");
    rejectSource("asset t{size 1x1 output normal relief draw{layer n{}relief=mask.from(n)}}", "output 'relief' expects layer, got mask");
    rejectSource("asset t{size 1x1 draw{layer n(encoding=surface.source_over){}}}", "layer encoding must be surface.normal");
    rejectSource("asset t{size 1x1 draw{layer n(encoding=normal){}}}", "'.' in surface encoding");
    rejectSource("asset t{size 1x1 draw{layer n(alpha=surface.source_over,encoding=surface.normal){}}}", "either 'alpha' or 'encoding'");
    rejectSource("asset t{size 1x1 draw{layer n(encoding=surface.normal,alpha=surface.legacy){}}}", "either 'alpha' or 'encoding'");
    rejectSource("asset t{size 1x1 draw{layer n(mode=surface.normal){}}}", "layer supports only the 'alpha', 'encoding' and 'tile' options");
    rejectSource("asset t{size 1x1 draw{layer n{}x=n.normal()}}", ".normal takes an f32 depth");
    rejectSource("asset t{size 1x1 draw{layer n{}x=n.normal(1,2,3)}}", ".normal takes an f32 depth");
    rejectSource("asset t{size 1x1 draw{layer n{}x=n.normal(sigma=1)}}", ".normal takes an f32 depth");
    rejectSource("asset t{size 1x1 draw{layer n{}x=n.normal(smoothing=1,depth=2)}}", ".normal takes an f32 depth");
    rejectSource("asset t{size 1x1 draw{layer n{}x=n.normal(#fff)}}", ".normal depth must be f32, got color");
    rejectSource("asset t{size 1x1 draw{layer n{}x=n.normal(1,{1,2})}}", ".normal smoothing must be f32, got vec2");
    rejectSource("asset t{size 1x1 draw{x=4.0 y=x.normal(1)}}", ".normal needs a layer or mask receiver, got f32");
    rejectSource("asset t{size 1x1 draw{layer n{}x=n.normal(-1)}}", "normal depth must be finite and nonnegative");
    rejectSource("asset t{size 1x1 draw{layer n{}x=n.normal(1,64.5)}}", "normal smoothing must be finite and in 0..64");
    rejectSource("asset t{size 1x1 draw{layer n{}x=n.normal(1,-0.5)}}", "normal smoothing must be finite and in 0..64");
    rejectSource("asset t{size 1x1 draw{x=color.normal(0,0,0)}}", "color.normal needs a finite nonzero direction with z > 0");
    rejectSource("asset t{size 1x1 draw{x=color.normal(1,0,0)}}", "color.normal needs a finite nonzero direction with z > 0");
    rejectSource("asset t{size 1x1 draw{x=color.normal(0.5,0.5,-1)}}", "color.normal needs a finite nonzero direction with z > 0");
    rejectSource("asset t{size 1x1 const color bad=color.normal(0,0,0) draw{}}", "color.normal needs a finite nonzero direction with z > 0");
    rejectSource("asset t{size 1x1 draw{x=color.normal(1,2)}}", "color.normal");
    rejectSource("asset t{size 1x1 draw{x=color.normal(#fff,0,1)}}", "color.normal");
    rejectSource("asset t{size 1x1 output normal relief " + body.substr(0, body.size() - 1) + " helper()->layer{return relief}}", "relief");
    // `normal` stays an ordinary name everywhere else.
    CHECK_NOTHROW(compileChecked("asset t{size 1x1 input f32 normal=1 output point part normal2(normal:f32)->f32{return normal} "
        "draw{depth=normal2(normal) smoothing=depth encoding=smoothing part={encoding,0}}}"));
    CHECK_NOTHROW(compileChecked("asset normal{size 1x1 output normal normal draw{layer n(encoding=surface.normal){}normal=n}}"));
}

TEST_CASE("misused direction layers trap with the contract's messages", "[relief]") {
    CHECK(trapOf(compileChecked("asset t{size 2x2 output normal relief draw{layer n{}relief=n}}")) ==
        "normal output needs a direction layer");
    CHECK(trapOf(compileChecked("asset t{size 2x2 output normal relief draw{layer n(alpha=surface.source_over){}relief=n}}")) ==
        "normal output needs a direction layer");
    CHECK(trapOf(compileChecked("asset t{size 2x2 draw{layer n(encoding=surface.normal){weather(1,2)}}}")) ==
        "weather cannot run in a direction capture");
    CHECK(trapOf(compileChecked("asset t{size 2x2 draw{layer n(encoding=surface.normal){}x=n.normal(1.0)}}")) ==
        "normal needs a height layer, not a direction layer");
    CHECK(trapOf(compileChecked("asset t{size 2x2 draw{layer n(encoding=surface.normal){pixel(0,0,#8080ff)}p=part.create(n)}}")) ==
        "dynamic parts do not support direction layers");
    const auto dynamic = compileChecked("asset t{size 2x2 input f32 depth=1 input f32 smoothing=0 draw{layer n{}x=n.normal(depth,smoothing)}}");
    CHECK(trapOf(dynamic).empty());
    CHECK(trapOf(dynamic, {{"depth", -1.f}}) == "normal depth must be finite and nonnegative");
    CHECK(trapOf(dynamic, {{"smoothing", 65.f}}) == "normal smoothing must be finite and in 0..64");
    CHECK(trapOf(dynamic, {{"depth", -1.f}, {"smoothing", 65.f}}) == "normal smoothing must be finite and in 0..64");
    // An unset layer traps like any unassigned handle.
    CHECK(trapOf(compileChecked("asset t{size 2x2 draw{if(false){layer n{}}x=n.normal(1.0)}}")) == "layer is unassigned");
    // A blurred or copied direction layer is still one; a mask of it is coverage.
    CHECK(trapOf(compileChecked("asset t{size 2x2 output normal relief draw{layer n(encoding=surface.normal){pixel(0,0,#8080ff)}"
        "copy=n.blur(0.5) m=mask.from(copy) again=m.normal(1.0) relief=copy}}")).empty());
}

TEST_CASE("normal sources survive formatting, tools, debug stripping and compression", "[relief]") {
    const std::string source = "asset t{size 6x6 output normal relief draw{layer body{rect(1,1,4,4,#fff)}surface.paint(body)"
        "layer n(encoding=surface.normal){surface.paint(mask.from(body).normal(depth=3.0,smoothing=1.0))"
        "surface.paint(body.normal(2))pixel(2,2,color.normal(-0.5,0.0,0.8))}relief=n}}";
    const auto bytes = compileChecked(source, false);
    const auto expected = normalOf(produce(bytes)).pixels;
    CHECK(expected != std::vector<Color>(36, kFlatDirection));
    CHECK(normalOf(produce(compileChecked(source, true))).pixels == expected);

    auto program = ips::parse(source);
    const auto formatted = ips::formatProgram(program);
    CHECK(formatted.find("output normal relief") != std::string::npos);
    CHECK(formatted.find("layer n(encoding = surface.normal) {") != std::string::npos);
    CHECK(formatted.find("mask.from(body).normal(depth = 3.0, smoothing = 1.0)") != std::string::npos);
    CHECK(formatted.find("body.normal(2)") != std::string::npos);
    CHECK(formatted.find("color.normal((-0.5), 0.0, 0.8)") != std::string::npos);
    auto again = ips::parse(formatted);
    CHECK(ips::formatProgram(again) == formatted);
    CHECK(normalOf(produce(compileChecked(formatted, false))).pixels == expected);

    const auto stripped = stripDebug(bytes);
    const auto text = disassemble(stripped);
    CHECK(text.find("export: normal relief root") != std::string::npos);
    CHECK(text.find("layer.begin 1 3") != std::string::npos);
    CHECK(text.find("surface.normal 1") != std::string::npos);
    CHECK(text.find("surface.normal\n") != std::string::npos);
    // Reassembly keeps the program; its string pool drops unused local names.
    const auto restored = assemble(text);
    CHECK(disassemble(restored) == text);
    CHECK(normalOf(produce(restored)).pixels == expected);
    CHECK(normalOf(produce(stripped)).pixels == expected);
    CHECK(normalOf(produce(compressFile(bytes))).pixels == expected);
    CHECK(stripDebug(decompressFile(compressFile(bytes))) == stripped);
}
