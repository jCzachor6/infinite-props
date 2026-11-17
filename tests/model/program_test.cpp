// Model programs end to end (spec models v1.2): a source with `render` is
// compiled with its skins into one .iprop3d file, loaded, validated and
// generated; the results are checked against what the source says.
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "SourceCompilation.hpp"
#include "iprop/VM.hpp"
#include "iprop/model/Model.hpp"

#include <cstring>
#include <map>
#include <string>
#include <tuple>

using namespace iprop;
using Catch::Matchers::ContainsSubstring;

namespace {

using Sources = std::map<std::string, std::string>;

const char* const kOak = R"(
asset oak {
  size 64x32
  input color tint = #8a5a2b
  input f32 wear = 0.25
  output normal relief
  draw {
    root = seed.initial()
    rect(0, 0, 64, 32, color.shade(tint, -55))
    for i in 0..4 {
      tone = int(roll(root.fork(i), -14.0, 14.0))
      rect(float(i) * 16.0 + 1.0, 0, 15, 32, color.shade(tint, tone))
    }
    layer height(alpha = surface.source_over) {
      rect(0, 0, 64, 32, #606060)
      for i in 0..4 { rect(float(i) * 16.0 + 1.0, 0, 15, 32, #ffffff) }
    }
    relief = height.normal(2.0 + wear, 0.7)
  }
}
)";

const char* const kPlain = R"(
asset plain {
  size 8x8
  draw { rect(0, 0, 8, 8, #808080) }
}
)";

SourceCompilationResult compile(const Sources& sources, const std::string& entry = "model.ips") {
    SourceCompilationOptions options;
    return compileSource(entry, [&](std::string_view path) {
        const auto found = sources.find(std::string(path));
        if (found == sources.end()) throw std::runtime_error("no source " + std::string(path));
        return found->second;
    }, options);
}

model::LoadedModel load(const Sources& sources) {
    const auto compiled = compile(sources);
    INFO((compiled.diagnostics.empty() ? std::string("no diagnostic") : compiled.diagnostics.front().message));
    REQUIRE(compiled.succeeded());
    REQUIRE(compiled.model);
    auto loaded = model::loadModel(compiled.bytes);
    model::validateModel(loaded);
    return loaded;
}

model::ModelResult generate(const Sources& sources, u32 seed = 7) {
    const auto loaded = load(sources);
    model::ModelInputs inputs;
    inputs.seed = seed;
    return model::generateModel(loaded, inputs);
}

std::string diagnostic(const Sources& sources) {
    const auto compiled = compile(sources);
    REQUIRE_FALSE(compiled.succeeded());
    REQUIRE_FALSE(compiled.diagnostics.empty());
    return compiled.diagnostics.front().message;
}

std::string thing(const std::string& body, const std::string& declarations = "", const std::string& helpers = "") {
    return "import wood from \"oak.ips\"\nimport flat from \"plain.ips\"\nasset thing {\n  size 40x40x40\n" +
           declarations + "\n  render {\n" + body + "\n  }\n" + helpers + "\n}\n";
}

Sources with(const std::string& source) { return {{"model.ips", source}, {"oak.ips", kOak}, {"plain.ips", kPlain}}; }

std::string trapMessage(const Sources& sources, u32 seed = 1) {
    const auto loaded = load(sources);
    model::ModelInputs inputs;
    inputs.seed = seed;
    try {
        (void)model::generateModel(loaded, inputs);
    } catch (const FormatError& error) {
        return error.what();
    }
    return "no trap";
}

} // namespace

TEST_CASE("a model compiles to an .iprop3d file and generates") {
    const auto sources = with(thing(R"(
    w = 24.0 to 36.0
    oak = wood.draw(123, tint = stain)
    box({0, 0, 0}, {w, 10, 20}, oak)
)", "  scale 50\n  input color stain = #8a5a2b"));
    const auto compiled = compile(sources);
    REQUIRE(compiled.succeeded());
    CHECK(compiled.model);
    // The 2D loader rejects the file at its magic, and the model loader a 2D file.
    CHECK_THROWS_WITH(loadFile(compiled.bytes), ContainsSubstring("bad magic"));
    const auto skin = compile(sources, "oak.ips");
    REQUIRE(skin.succeeded());
    CHECK_FALSE(skin.model);
    CHECK_THROWS_WITH(model::loadModel(skin.bytes), ContainsSubstring("2D .iprop program"));

    const auto loaded = model::loadModel(compiled.bytes);
    model::validateModel(loaded);
    CHECK(loaded.size[0] == 40.f);
    CHECK(loaded.size[2] == 40.f);
    CHECK(loaded.unitsPerMeter == 50.f);
    REQUIRE(loaded.skins.size() == 2);
    CHECK(loaded.skins[0].name == "wood");

    model::ModelInputs inputs;
    inputs.seed = 7;
    const auto result = model::generateModel(loaded, inputs);
    CHECK(result.vertexCount == 24);
    CHECK(result.triangleCount == 12);
    REQUIRE(result.surfaces.size() == 1);
    REQUIRE(result.textures.size() == 1);
    const auto& surface = result.surfaces[0];
    CHECK(surface.positions.size() == 24 * 3);
    CHECK(surface.tangents.size() == 24 * 4);
    CHECK(surface.indices.size() == 36);
    CHECK(surface.roughness == 0.8f);
    CHECK(result.boundsMin[1] == 0.f);
    CHECK(result.boundsMax[1] == 10.f);
    CHECK(result.boundsMax[2] == 10.f);
    CHECK(result.boundsMax[0] >= 12.f);
    CHECK(result.boundsMax[0] <= 18.f);
    CHECK(result.unitsPerMeter == 50.f);
    CHECK(result.fuelUsed > 36);
    CHECK(result.workUsed > 64 * 32);

    const auto& texture = result.textures[0];
    CHECK(texture.name == "wood");
    CHECK(texture.color.width == 64);
    CHECK(texture.color.height == 32);
    CHECK(texture.assetWidth == 64);
    CHECK(texture.repeats);
    REQUIRE(texture.images.size() == 1);
    CHECK(texture.images[0].kind == AuxImageKind::Normal);

    // The same seed gives the same bits; another seed another width.
    const auto again = model::generateModel(loaded, inputs);
    CHECK(again.surfaces[0].positions == surface.positions);
    CHECK(again.textures[0].key == texture.key);
    inputs.seed = 8;
    CHECK(model::generateModel(loaded, inputs).surfaces[0].positions != surface.positions);
}

TEST_CASE("a skin is the 2D generation of its program") {
    const auto sources = with(thing(R"(
    oak = wood.draw(123, tint = #336699, wear = 0.5, size = {128, 64})
    box({0, 0, 0}, {4, 4, 4}, oak)
)"));
    const auto result = generate(sources);
    REQUIRE(result.textures.size() == 1);
    const auto& texture = result.textures[0];
    CHECK(texture.color.width == 128);
    CHECK(texture.assetWidth == 64);

    // The standalone program, with the same seed, size and inputs.
    const auto standalone = compile(sources, "oak.ips");
    REQUIRE(standalone.succeeded());
    const auto file = loadFile(standalone.bytes);
    validate(file);
    GenerationInputs inputs;
    inputs.seed = 123;
    inputs.sizeX = 128;
    inputs.sizeY = 64;
    UniformValue tint;
    tint.type = InputType::Color;
    tint.color = Color{0x33, 0x66, 0x99, 0xff};
    UniformValue wear;
    wear.type = InputType::F32;
    wear.f = 0.5f;
    inputs.uniforms = {{"tint", tint}, {"wear", wear}};
    VM vm(file, inputs);
    vm.run();
    const auto expected = vm.takeResult();
    REQUIRE(expected.image.pixels.size() == texture.color.pixels.size());
    CHECK(std::memcmp(expected.image.pixels.data(), texture.color.pixels.data(),
                      expected.image.pixels.size() * sizeof(Color)) == 0);
    REQUIRE(expected.images.size() == 1);
    CHECK(std::memcmp(expected.images[0].image.pixels.data(), texture.images[0].image.pixels.data(),
                      expected.images[0].image.pixels.size() * sizeof(Color)) == 0);
}

TEST_CASE("skin records share a texture and split surfaces by material") {
    const auto result = generate(with(thing(R"(
    oak = wood.draw(5)
    same = wood.draw(5)
    other = wood.draw(6)
    rough = skin.roughness(oak, 0.3)
    moved = skin.shift(skin.fit(oak), {8, 0})
    box({0, 0, 0}, {4, 4, 4}, oak)
    box({8, 0, 0}, {4, 4, 4}, same)
    box({16, 0, 0}, {4, 4, 4}, moved)
    box({24, 0, 0}, {4, 4, 4}, rough)
    box({32, 0, 0}, {4, 4, 4}, other)
)")));
    // Two generations: seeds 5 and 6. The repeated call reuses the first.
    CHECK(result.textures.size() == 2);
    // A shift or a fit bakes into the UVs; roughness and the texture do not.
    REQUIRE(result.surfaces.size() == 3);
    CHECK(result.surfaces[0].indices.size() == 3 * 36);
    CHECK(result.surfaces[1].roughness == 0.3f);
    CHECK(result.surfaces[1].texture == 0);
    CHECK(result.surfaces[2].texture == 1);
    CHECK(result.vertexCount == 5 * 24);
}

TEST_CASE("every skin modifier reaches the result") {
    const auto result = generate(with(thing(R"(
    oak = wood.draw(1)
    quad({0, 0, 0}, {64, 0, 0}, {64, 32, 0}, {0, 32, 0}, oak)
    quad({0, 0, 1}, {64, 0, 1}, {64, 32, 1}, {0, 32, 1}, skin.density(oak, 2.0))
    quad({0, 0, 2}, {64, 0, 2}, {64, 32, 2}, {0, 32, 2}, skin.shift(oak, {16, 8}))
    quad({0, 0, 3}, {10, 0, 3}, {10, 5, 3}, {0, 5, 3}, skin.fit(oak))
    quad({0, 0, 4}, {1, 0, 4}, {1, 1, 4}, {0, 1, 4}, skin.metallic(skin.roughness(oak, 0.25), 0.75))
    quad({0, 0, 5}, {1, 0, 5}, {1, 1, 5}, {0, 1, 5}, skin.emission(oak, 2.5))
    quad({0, 0, 6}, {1, 0, 6}, {1, 1, 6}, {0, 1, 6}, skin.two_sided(skin.cutout(oak, 0.5)))
    quad({0, 0, 7}, {1, 0, 7}, {1, 1, 7}, {0, 1, 7}, skin.crisp(oak))
)")));
    // Density, shift and fit only move UVs: the first four quads share a surface.
    REQUIRE(result.surfaces.size() == 5);
    const auto& mapped = result.surfaces[0];
    REQUIRE(mapped.uvs.size() == 4 * 4 * 2);
    // A quad's corners are lower left, lower right, upper right, upper left.
    const auto uv = [&](std::size_t quad, std::size_t corner) {
        return std::pair<f32, f32>{mapped.uvs[(quad * 4 + corner) * 2], mapped.uvs[(quad * 4 + corner) * 2 + 1]};
    };
    CHECK(uv(0, 3) == std::pair<f32, f32>{0.f, 0.f});
    CHECK(uv(0, 1) == std::pair<f32, f32>{1.f, 1.f});   // 64 x 32 units on a 64 x 32 image: once
    CHECK(uv(1, 1) == std::pair<f32, f32>{2.f, 2.f});   // twice the texels to a unit
    CHECK(uv(2, 3) == std::pair<f32, f32>{0.25f, 0.25f});
    CHECK(uv(2, 1) == std::pair<f32, f32>{1.25f, 1.25f});
    CHECK(uv(3, 1) == std::pair<f32, f32>{1.f, 1.f});   // fit: the image once, whatever the size
    CHECK(result.textures[0].repeats);

    CHECK(result.surfaces[1].roughness == 0.25f);
    CHECK(result.surfaces[1].metallic == 0.75f);
    CHECK(result.surfaces[2].emission == 2.5f);
    CHECK(result.surfaces[3].alpha == model::SkinAlpha::Cutout);
    CHECK(result.surfaces[3].cutoff == 0.5f);
    CHECK(result.surfaces[3].twoSided);
    CHECK(result.surfaces[4].crisp);
    CHECK_FALSE(result.surfaces[0].crisp);
    CHECK(result.surfaces[0].alpha == model::SkinAlpha::Opaque);
    CHECK(result.textures.size() == 1);

    // In a draw asset `skin` is an ordinary name and `density` stays a keyword elsewhere.
    CHECK_THAT(diagnostic({{"model.ips", "asset flat {\n  size 8x8\n  draw {\n    s = skin.density(1, 2.0)\n  }\n}\n"}}),
               ContainsSubstring("skin"));
}

TEST_CASE("place moves, turns, mirrors and unwinds") {
    const auto result = generate(with(thing(R"(
    oak = wood.draw(1)
    place(at = {10, 0, 0}) {
      place(yaw = 90.0) { box({0, 0, 5}, {2, 2, 2}, oak) }
    }
    for i in 0..4 {
      place(at = {0, float(i) * 10.0, 0}, mirror_x = 1) {
        if (i == 2) { break }
        box({0, 0, 0}, {1, 1, 1}, oak)
      }
    }
    lone(oak)
)", "", R"(
  lone(s: skin) {
    place(at = {0, 100, 0}) {
      box({0, 0, 0}, {1, 1, 1}, s)
      return
    }
  }
)")));
    // The first box: (0, 0, 5) turned a quarter about +Y is (5, 0, 0), then moved by 10.
    const auto& positions = result.surfaces[0].positions;
    f32 minX = 1e9f, maxX = -1e9f;
    for (std::size_t i = 0; i < 24; ++i) {
        minX = std::min(minX, positions[i * 3]);
        maxX = std::max(maxX, positions[i * 3]);
    }
    CHECK(minX == 14.f);
    CHECK(maxX == 16.f);
    // Two boxes before the break, and the helper's box after its return.
    CHECK(result.vertexCount == 24 * 4);
    CHECK(result.boundsMax[1] == 101.f);
    // A mirrored box has the opposite tangent sign.
    CHECK(result.surfaces[0].tangents[3] == 1.f);
    CHECK(result.surfaces[0].tangents[24 * 4 + 3] == -1.f);
}

TEST_CASE("every primitive and the vec3 operations compile and emit") {
    const auto result = generate(with(thing(R"(
    oak = wood.draw(1)
    up = vec3.normalize(vec3.cross({1, 0, 0}, {0, 0, -1}))
    base = {1, 2, 3} + up * 2.0 - {0, 1, 0} / 2.0
    reach = vec3.len(base) + vec3.dot(up, up) + base.x + base.y + base.z
    mid = lerp({0, 0, 0}, base, 0.5)
    spot = -mid to mid
    box(spot, {4, 4, 4}, oak)
    box({0, 0, 0}, {4, 4, 4}, 0.5, oak)
    cylinder({10, 0, 0}, 2, 5, 8, oak)
    cone({20, 0, 0}, 2, 1, 5, 8, oak)
    band({30, 0, 0}, 2, 2, 1, 8, oak)
    sphere({40, 0, 0}, 2, 8, 4, oak)
    lathe({50, 0, 0}, [{2, 0}, {3, 2}, {2, 4}], 8, oak)
    lathe({60, 0, 0}, [{2, 0}, {3, 2}, {2, 4}], 8, 60.0, oak)
    extrude({70, 0, 0}, [{0, 0}, {4, 0}, {4, 3}, {0, 3}], 2, oak)
    extrude({80, 0, 0}, [{0, 0}, {4, 0}, {4, 3}, {0, 3}], 2, 0.25, oak)
    quad({90, 0, 0}, {94, 0, 0}, {94, 3, 0}, {90, 3, 0}, oak)
    tri({100, 0, 0}, {104, 0, 0}, {102, 3, 0}, oak)
    if (reach > 1000.0) { box({0, 0, 0}, {1, 1, 1}, oak) }
)")));
    const u32 expected = 24 + 96 + 4 * 9 + 4 * 9 + 2 * 9 + 9 * 5 + (4 * 9 + 2 * 9) + (3 * 9 + 2 * 9) + 24 + 56 + 4 + 3;
    CHECK(result.vertexCount == expected);
    CHECK(result.surfaces.size() == 1);
}

TEST_CASE("vec3 constants are the values the runtime computes") {
    // Every constant beside the same expression made of a value the compiler
    // cannot know: the bits must agree.
    const auto result = generate(with(thing(R"(
    oak = wood.draw(1)
    z = float(zero)
    corner2 = {1 + z, 2.5, 3}
    up2 = vec3.normalize(vec3.cross({1 + z, 0, 0}, {0, 0, -1}))
    far2 = corner2 * 2.0 + up2 - {z, 1, 0} / 3.0
    reach2 = vec3.len(far2) + far2.z + vec3.dot(corner2, far2)
    mid2 = lerp(corner2, far2, 0.3)
    flipped2 = -mid2 * {1, 2, 3} / {3, 2, 1} + 0.1 * corner2
    tri(corner, far, mid, oak)
    tri(corner2, far2, mid2, oak)
    tri(flipped, up, {reach, reach, reach}, oak)
    tri(flipped2, up2, {reach2, reach2, reach2}, oak)
)", R"(  input u32 zero = 0
  const vec3 corner = {1, 2.5, 3}
  const vec3 up = vec3.normalize(vec3.cross({1, 0, 0}, {0, 0, -1}))
  const vec3 far = corner * 2.0 + up - {0, 1, 0} / 3.0
  const f32 reach = vec3.len(far) + far.z + vec3.dot(corner, far)
  const vec3 mid = lerp(corner, far, 0.3)
  const vec3 flipped = -mid * {1, 2, 3} / {3, 2, 1} + 0.1 * corner)")));
    REQUIRE(result.surfaces.size() == 1);
    const auto& positions = result.surfaces[0].positions;
    REQUIRE(positions.size() == 4 * 9);
    const auto triangle = [&](std::size_t index) {
        return std::vector<u32>(reinterpret_cast<const u32*>(positions.data()) + index * 9,
                                reinterpret_cast<const u32*>(positions.data()) + index * 9 + 9);
    };
    CHECK(triangle(0) == triangle(1));
    CHECK(triangle(2) == triangle(3));
    CHECK(positions[0] == 1.f);
    CHECK(positions[1] == 2.5f);
    CHECK(positions[2] == 3.f);
    CHECK(positions[18 + 3] == 0.f); // up = (0, 1, 0)
    CHECK(positions[18 + 4] == 1.f);

    // What a constant cannot be, said where it is declared.
    CHECK_THAT(diagnostic(with(thing("", "  const vec3 n = vec3.normalize({1.0 / 0.0, 0, 0})"))),
               ContainsSubstring("vec3.normalize needs finite components"));
    CHECK_THAT(diagnostic(with(thing("", "  const f32 n = float(box({0, 0, 0}, {1, 1, 1}, 1))"))),
               ContainsSubstring("'box' has RNG or generation effects"));
    CHECK_THAT(diagnostic(with(thing("", "  const skin s = wood.draw(1)"))), ContainsSubstring("not allowed"));
    CHECK_THAT(diagnostic(with(thing("", "  const vec3 n = {1, 2}"))), ContainsSubstring("needs a vec3, got vec2"));
}

TEST_CASE("a vec3 range picks a point between its ends") {
    const auto result = generate(with(thing(R"(
    oak = wood.draw(1)
    for i in 0..12 {
      p = {0, 0, 0} to {4.0, 0, 8.0}
      q = lerp({0, 0, 0}, {1, 1, 1}, 0.5) to {2, 2, 2}
      r = {0.0 to 4.0, 0, 0.0 to 8.0}
      tri(p, q, r, oak)
    }
    a = pick({1, 0, 0}, {0, 1, 0})
    b = choose(seed.initial().fork("b"), {5, 5, 5}, {6, 6, 6})
    c = {0, 0, 0} to {1, 1, 1} with gaussian(0.5, 0.1)
    tri(a, b, c, oak)
)")));
    const auto& positions = result.surfaces[0].positions;
    REQUIRE(positions.size() == 13 * 9);
    bool spread = false, offSegment = false;
    for (std::size_t i = 0; i < 12; ++i) {
        const f32* p = &positions[i * 9];
        const f32* q = p + 3;
        const f32* r = p + 6;
        // One draw places p on the segment: z is twice x.
        CHECK(p[1] == 0.f);
        CHECK(p[2] == p[0] * 2.f);
        CHECK(p[0] >= 0.f);
        CHECK(p[0] <= 4.f);
        CHECK(q[0] == q[1]);
        CHECK(q[1] == q[2]);
        CHECK(q[0] >= 0.5f);
        CHECK(q[0] <= 2.f);
        // Two ranges in a literal are two draws: a point of the rectangle.
        spread = spread || p[0] != positions[0];
        offSegment = offSegment || r[2] != r[0] * 2.f;
    }
    CHECK(spread);
    CHECK(offSegment);
    const f32* a = &positions[12 * 9];
    CHECK(a[0] + a[1] == 1.f);
    CHECK(a[2] == 0.f);
    CHECK((a[3] == 5.f || a[3] == 6.f));
    CHECK(a[3] == a[4]);
    CHECK(a[6] == a[7]);
    CHECK(a[6] > 0.f);
    CHECK(a[6] < 1.f);

    // A vec3 is not an element of a sampler, a stepper or a list in M1.
    CHECK_THAT(diagnostic(with(thing("    s = sampler({0, 0, 0} to {1, 1, 1})"))),
               ContainsSubstring("sampler capture needs i32, f32, color or vec2, got vec3"));
    CHECK_THAT(diagnostic(with(thing("    s = stepper({0, 0, 0} to {1, 1, 1}).pick(3)"))),
               ContainsSubstring("stepper capture needs i32, f32, color or vec2, got vec3"));
    // A list of vec3 is a path (M3); no sampler holds one.
    CHECK_THAT(diagnostic(with(thing("", "", "  f(points: sampler<vec3>) { }"))),
               ContainsSubstring("a collection cannot hold 'vec3' values"));
    CHECK_THAT(diagnostic({{"model.ips", "asset flat {\n  size 8x8\n  draw { }\n  f(points: list<vec3>) { }\n}\n"}}),
               ContainsSubstring("type 'list<vec3>' belongs to a render asset"));
    CHECK_THAT(diagnostic(with(thing("    p = {1, 2, 3} % 2"))), ContainsSubstring("'%' requires i32 operands"));
    CHECK_THAT(diagnostic(with(thing("    if ({1, 2, 3} < {2, 3, 4}) { }"))), ContainsSubstring("comparison requires numeric operands"));
}

TEST_CASE("the checker keeps drawing and modelling apart") {
    CHECK_THAT(diagnostic(with(thing("    rect(0, 0, 4, 4, #fff)"))), ContainsSubstring("render asset has no canvas"));
    CHECK_THAT(diagnostic(with(thing("    layer a { }"))), ContainsSubstring("render asset has no canvas"));
    CHECK_THAT(diagnostic(with(thing("    m = noise.mask({0, 0}, {4, 4}, noise.mode_value, {1, 1}, 0.5, 1)"))),
               ContainsSubstring("render asset"));
    CHECK_THAT(diagnostic(with(thing("    place() { }"))), ContainsSubstring("at least one"));
    CHECK_THAT(diagnostic(with(thing("    place(at = {1, 2}) { }"))), ContainsSubstring("'at' must be a vec3"));
    CHECK_THAT(diagnostic(with(thing("    place(tilt = 3) { }"))), ContainsSubstring("unknown argument 'tilt'"));
    CHECK_THAT(diagnostic(with(thing("    place(yaw = 1, yaw = 2) { }"))), ContainsSubstring("duplicate argument"));
    CHECK_THAT(diagnostic(with(thing("    s = wood.draw(1, at = {0, 0})"))), ContainsSubstring("a skin has no placement"));
    CHECK_THAT(diagnostic(with(thing("    s = wood.draw(1, colour = #fff)"))), ContainsSubstring("colour"));
    CHECK_THAT(diagnostic(with(thing("    box({0, 0, 0}, {1, 1, 1}, 5)"))), ContainsSubstring("skin"));
    CHECK_THAT(diagnostic(with(thing("    box({0, 0}, {1, 1, 1}, wood.draw(1))"))), ContainsSubstring("vec3"));

    const std::string draw = "asset flat {\n  size 8x8\n  draw {\n";
    CHECK_THAT(diagnostic({{"model.ips", draw + "    box({0, 0, 0}, {1, 1, 1}, 1)\n  }\n}\n"}}),
               ContainsSubstring("available in a render asset"));
    CHECK_THAT(diagnostic({{"model.ips", draw + "    place(yaw = 1) { }\n  }\n}\n"}}),
               ContainsSubstring("available in a render asset"));
    CHECK_THAT(diagnostic({{"model.ips", "asset both {\n  size 8x8x8\n  draw { }\n  render { }\n}\n"}}),
               ContainsSubstring("either 'draw' or 'render'"));
    CHECK_THAT(diagnostic({{"model.ips", "asset flat {\n  size 8x8\n  render { }\n}\n"}}),
               ContainsSubstring("size WxHxD"));
    CHECK_THAT(diagnostic({{"model.ips", "asset deep {\n  size 8x8x8\n  draw { }\n}\n"}}),
               ContainsSubstring("belongs to a render asset"));
    CHECK_THAT(diagnostic({{"model.ips", "asset out {\n  size 8x8x8\n  output point p\n  render { }\n}\n"}}),
               ContainsSubstring("a render asset declares point3d and box3d outputs"));
    CHECK_THAT(diagnostic({{"model.ips", "asset zero {\n  size 8x8x8\n  scale 0\n  render { }\n}\n"}}),
               ContainsSubstring("positive number"));
    // Inputs belong to the entry block, whatever its name, and the entry is
    // no helper.
    CHECK_THAT(diagnostic(with(thing("    part(wood.draw(1))", "  input f32 tall = 2.0",
                                     "  part(s: skin) { box({0, 0, 0}, {1, tall, 1}, s) }"))),
               ContainsSubstring("input 'tall' is only visible inside 'render'"));
    CHECK_THAT(diagnostic(with(thing("    render()"))), ContainsSubstring("'render' is an entry block; call a helper instead"));
    // A model is neither a part of a draw asset nor a skin of another model,
    // used or not.
    const std::string solid = "asset solid {\n  size 4x4x4\n  render { }\n}\n";
    CHECK_THAT(diagnostic({{"model.ips", "import solid from \"solid.ips\"\nasset flat {\n  size 8x8\n  draw { }\n}\n"},
                           {"solid.ips", solid}}),
               ContainsSubstring("'solid' is a render asset; a draw asset cannot use a 3D model as a part"));
    // A model imports a render asset as a 3D part (M3), not as a skin.
    CHECK_THAT(diagnostic({{"model.ips", "import solid from \"solid.ips\"\nasset deep {\n  size 8x8x8\n  render { s = solid.draw(1) }\n}\n"},
                           {"solid.ips", solid}}),
               ContainsSubstring("'solid' is a render asset; build it with solid.render(seed, ...)"));
    // The types of render assets are no types of a draw asset: not in a
    // helper nobody calls, a result, a typed local or a constant.
    const auto flat = [](const std::string& declarations, const std::string& body, const std::string& helpers) {
        return Sources{{"model.ips", "asset flat {\n  size 8x8\n" + declarations + "\n  draw {\n" + body + "\n  }\n" + helpers + "\n}\n"}};
    };
    for (const std::string type : {"vec3", "skin"}) {
        const std::string message = "type '" + type + "' belongs to a render asset; this asset has 'draw'";
        CHECK_THAT(diagnostic(flat("", "", "  f(p: " + type + ") { }")), ContainsSubstring(message));
        CHECK_THAT(diagnostic(flat("", "", "  f() -> " + type + " { return f() }")), ContainsSubstring(message));
        CHECK_THAT(diagnostic(flat("", "    " + type + " p = 1", "")), ContainsSubstring(message));
        CHECK_THAT(diagnostic(flat("  const " + type + " n = 1", "", "")), ContainsSubstring(message));
    }
    // A draw asset still names its helpers and locals as it likes.
    const auto free = compile({{"model.ips", "asset flat {\n  size 8x8\n  draw {\n    skin = 3\n    vec3 = box(skin)\n"
                                             "    rect(0, 0, float(vec3), 1, #fff)\n  }\n  box(n: i32) -> i32 { return n + 1 }\n}\n"}});
    CHECK(free.succeeded());
}

TEST_CASE("the model tariff charges vertices, triangles, outlines and scopes") {
    // Two programs that execute the same instructions apart from one
    // argument differ in fuel by exactly the tariff of what they emit.
    const auto spent = [](const std::string& body) {
        const auto result = generate(with(thing("    s = wood.draw(1)\n" + body)));
        return std::tuple<u64, u64, u64>{result.fuelUsed, result.vertexCount, result.triangleCount};
    };
    const auto emitted = [](u64 vertices, u64 triangles) {
        return vertices * model::kVertexFuel + triangles * model::kTriangleFuel;
    };
    const auto [narrow, narrowVertices, narrowTriangles] = spent("    cylinder({0, 0, 0}, 1.0, 2.0, 8, s)");
    const auto [wide, wideVertices, wideTriangles] = spent("    cylinder({0, 0, 0}, 1.0, 2.0, 16, s)");
    CHECK(wide - narrow == emitted(wideVertices - narrowVertices, wideTriangles - narrowTriangles));
    CHECK(wide - narrow == 6 * 32 + 2 * 32);

    // A bevel adds one constant argument.
    const auto [box, boxVertices, boxTriangles] = spent("    box({0, 0, 0}, {2, 3, 4}, s)");
    const auto [bevelled, bevelledVertices, bevelledTriangles] = spent("    box({0, 0, 0}, {2, 3, 4}, 0.25, s)");
    CHECK(bevelled - box == 1 + emitted(bevelledVertices - boxVertices, bevelledTriangles - boxTriangles));

    // An extrude outline pays for its checks with the square of its points.
    CHECK(model::outlineFuel(5, false) == 9);
    CHECK(model::outlineFuel(5, true) == 13);
    CHECK(model::outlineFuel(256, false) == 21846);
    CHECK(model::outlineFuel(256, true) == 32768);
    const std::string house = "[{-5, 0}, {5, 0}, {5, 6}, {0, 11}, {-5, 6}]";
    const auto [plain, plainVertices, plainTriangles] = spent("    extrude({0, 0, 0}, " + house + ", 4.0, s)");
    const auto [cut, cutVertices, cutTriangles] = spent("    extrude({0, 0, 0}, " + house + ", 4.0, 0.5, s)");
    CHECK(cut - plain == 1 + emitted(cutVertices - plainVertices, cutTriangles - plainTriangles) +
                         model::outlineFuel(5, true) - model::outlineFuel(5, false));

    // A placement scope: the argument, place.begin with its reserve and end.
    const auto [once, onceVertices, onceTriangles] = spent("    p = {1, 2, 3}\n    place(at = p) { }");
    const auto [twice, twiceVertices, twiceTriangles] = spent("    p = {1, 2, 3}\n    place(at = p) { }\n    place(at = p) { }");
    CHECK(twice - once == 1 + (1 + model::kPlaceFuel) + 1);
    CHECK(onceVertices == 0);
    CHECK(twiceTriangles == 0);
    (void)narrowVertices; (void)onceTriangles; (void)twiceVertices;

    // The largest model the limits allow fits the default budget: every
    // vertex and triangle, and outlines of 256 points with a bevel as long
    // as their vertices last.
    const u64 outlines = model::kMaxVertices / (14 * 256);
    CHECK(model::kMaxVertices * model::kVertexFuel + model::kMaxTriangles * model::kTriangleFuel +
          outlines * model::outlineFuel(256, true) < 25'000'000);
}

TEST_CASE("a model without geometry or skins is valid") {
    const auto result = generate({{"model.ips", "asset empty {\n  size 1x1x1\n  render { }\n}\n"}});
    CHECK(result.vertexCount == 0);
    CHECK(result.surfaces.empty());
    CHECK(result.textures.empty());
    CHECK(result.boundsMax[0] == 0.f);
    CHECK(result.unitsPerMeter == 100.f);
}

TEST_CASE("violations trap and name their cause") {
    CHECK_THAT(trapMessage(with(thing("    s = wood.draw(1)\n    box({0, 0, 0}, {0, 1, 1}, s)"))),
               ContainsSubstring("box size must be positive"));
    CHECK_THAT(trapMessage(with(thing("    s = wood.draw(1)\n    cone({0, 0, 0}, 1, 1, 1, 2, s)"))),
               ContainsSubstring("sides must be in 3..64"));
    CHECK_THAT(trapMessage(with(thing("    s = wood.draw(1)\n    n = 0\n    place(scale = float(n)) { box({0, 0, 0}, {1, 1, 1}, s) }"))),
               ContainsSubstring("place scale must be positive"));
    CHECK_THAT(trapMessage(with(thing("    s = wood.draw(1, size = {4096, 16})"))), ContainsSubstring("skin size must be in 0..2048"));
    CHECK_THAT(trapMessage(with(thing("    s = skin.roughness(wood.draw(1), 2.0 - 0.0 * unit())"))),
               ContainsSubstring("skin roughness must be in 0..1"));
    CHECK_THAT(trapMessage(with(thing("", "", "  unused() -> skin { return wood.draw(1) }")), 1), ContainsSubstring("no trap"));
    // An unassigned skin: a local that no path assigned.
    CHECK_THAT(trapMessage(with(thing("    n = 0\n    if (n == 1) { s = wood.draw(1) }\n    box({0, 0, 0}, {1, 1, 1}, s)"))),
               ContainsSubstring("skin is unassigned"));
    // The vertex limit, reached by a loop.
    CHECK_THAT(trapMessage(with(thing("    s = wood.draw(1)\n    for i in 0..3000 { box({0, 0, 0}, {1, 1, 1}, s) }"))),
               ContainsSubstring("model exceeds 65536 vertices"));
    // The texture limit: 33 different seeds.
    CHECK_THAT(trapMessage(with(thing("    for i in 0..33 { s = flat.draw(i) }"))),
               ContainsSubstring("model exceeds 32 skin textures"));
}

TEST_CASE("limits of the host cover the skins too") {
    const auto loaded = load(with(thing("    s = wood.draw(1, size = {512, 256})\n    box({0, 0, 0}, {1, 1, 1}, s)")));
    model::ModelInputs inputs;
    inputs.limits.workLimit = 5000; // less than one pass over the texture
    try {
        (void)model::generateModel(loaded, inputs);
        FAIL("the work limit was not enforced");
    } catch (const FormatError& error) {
        CHECK(error.reason == ErrorReason::WorkExhausted);
        CHECK_THAT(error.what(), ContainsSubstring("skin 'wood'"));
    }
    auto token = std::make_shared<CancellationToken>();
    token->cancel();
    inputs.limits = {};
    inputs.limits.cancellation = token;
    CHECK_THROWS_AS(model::generateModel(loaded, inputs), FormatError);
}

TEST_CASE("root inputs reach the model by name") {
    const auto loaded = load(with(thing("    s = wood.draw(1)\n    box({0, 0, 0}, {wide, 1, 1}, s)",
                                         "  input f32 wide = 3")));
    model::ModelInputs inputs;
    CHECK(model::generateModel(loaded, inputs).boundsMax[0] == 1.5f);
    UniformValue wide;
    wide.type = InputType::F32;
    wide.f = 9.f;
    inputs.uniforms = {{"wide", wide}};
    CHECK(model::generateModel(loaded, inputs).boundsMax[0] == 4.5f);
}

namespace {

// A skin with every material map, and a sheet that names its layout.
const char* const kMetal = R"(
asset metal {
  size 16x8
  output roughness rough
  output metallic shine
  output ao shade
  output emission glow
  draw {
    rect(0, 0, 16, 8, #707478)
    layer r(alpha = surface.source_over) { rect(0, 0, 8, 8, #404040) }
    rough = r
    layer m(alpha = surface.source_over) { rect(0, 0, 16, 8, #ffffff) }
    shine = m
    layer a(alpha = surface.source_over) { rect(0, 4, 16, 4, #808080) }
    shade = a
    layer e(alpha = surface.source_over) { pixel(15, 7, #ff8000) }
    glow = e
  }
}
)";

const char* const kSheet = R"(
asset sheet {
  size 64x32
  const f32 frontX = 16.0
  const f32 frontW = 32.0
  const f32 rows = 16.0
  const vec2 badge = {6, 4}
  const list<vec2> outline = [{0, 0}, {4, 0}, {4, 3}, {0, 3}]
  draw { rect(0, 0, 64, 32, #a05030) }
}
)";

const char* const kDice = R"(
asset dice {
  size 8x8
  const distribution sizes = weighted {1: 50, 2: 50}
  const sampler<f32> jitter = sampler(0.0 to 1.0)
  draw { rect(0, 0, 8, 8, #808080) }
}
)";

Sources withMaps(const std::string& source) {
    return {{"model.ips", source}, {"oak.ips", kOak}, {"plain.ips", kPlain}, {"metal.ips", kMetal}, {"sheet.ips", kSheet},
            {"dice.ips", kDice}};
}
std::string mapped(const std::string& body, const std::string& helpers = "") {
    return "import metal from \"metal.ips\"\nimport sheet from \"sheet.ips\"\nimport flat from \"plain.ips\"\nasset thing {\n"
           "  size 40x40x40\n  render {\n" + body + "\n  }\n" + helpers + "\n}\n";
}

} // namespace

TEST_CASE("a skin's material maps reach its texture and start its numbers at one") {
    const auto result = generate(withMaps(mapped(R"(
    m = metal.draw(1)
    box({0, 0, 0}, {4, 4, 4}, m)
    box({8, 0, 0}, {4, 4, 4}, skin.roughness(skin.metallic(skin.emission(m, 3.0), 0.5), 0.25))
    box({16, 0, 0}, {4, 4, 4}, flat.draw(2))
)")));
    REQUIRE(result.textures.size() == 2);
    const auto& texture = result.textures[0];
    REQUIRE(texture.images.size() == 4);
    const std::vector<AuxImageKind> kinds{AuxImageKind::Roughness, AuxImageKind::Metallic, AuxImageKind::Ao, AuxImageKind::Emission};
    for (std::size_t i = 0; i < kinds.size(); ++i) {
        CHECK(texture.images[i].kind == kinds[i]);
        CHECK(texture.images[i].image.width == 16);
        CHECK(texture.images[i].image.height == 8);
    }
    CHECK(texture.images[0].image.at(0, 0).r == 0x40);
    CHECK(texture.images[0].image.at(8, 0).r == 255);
    CHECK(texture.images[3].image.at(15, 7) == Color{0xff, 0x80, 0x00, 255});
    REQUIRE(result.surfaces.size() == 3);
    // A map's number starts at 1; a modifier still sets it; a texture
    // without maps keeps the defaults of M1.
    CHECK(result.surfaces[0].roughness == 1.f);
    CHECK(result.surfaces[0].metallic == 1.f);
    CHECK(result.surfaces[0].emission == 1.f);
    CHECK(result.surfaces[1].roughness == 0.25f);
    CHECK(result.surfaces[1].metallic == 0.5f);
    CHECK(result.surfaces[1].emission == 3.f);
    CHECK(result.surfaces[2].roughness == model::kSkinRoughness);
    CHECK(result.surfaces[2].metallic == model::kSkinMetallic);
    CHECK(result.surfaces[2].emission == model::kSkinEmission);
}

TEST_CASE("skin.blend makes glass and skin.region shows a window once on every face") {
    const auto result = generate(withMaps(mapped(R"(
    s = sheet.draw(1)
    quad({0, 0, 0}, {10, 0, 0}, {10, 5, 0}, {0, 5, 0}, skin.region(s, 16.0, 8.0, 32.0, 16.0))
    quad({0, 0, 1}, {1, 0, 1}, {1, 1, 1}, {0, 1, 1}, skin.blend(skin.fit(s)))
    box({0, 0, 4}, {3, 2, 1}, 0.25, skin.region(s, 0.0, 0.0, 16.0, 16.0))
)")));
    REQUIRE(result.surfaces.size() == 2);
    CHECK(result.surfaces[1].alpha == model::SkinAlpha::Blend);
    const auto& surface = result.surfaces[0];
    // Lower left, lower right, upper right, upper left of the window.
    const auto uv = [&](std::size_t vertex) { return std::pair<f32, f32>{surface.uvs[vertex * 2], surface.uvs[vertex * 2 + 1]}; };
    CHECK(uv(0) == std::pair<f32, f32>{0.25f, 0.75f});
    CHECK(uv(1) == std::pair<f32, f32>{0.75f, 0.75f});
    CHECK(uv(2) == std::pair<f32, f32>{0.75f, 0.25f});
    CHECK(uv(3) == std::pair<f32, f32>{0.25f, 0.25f});
    // Every vertex of the bevelled box stays in its window, 0..1/4 by 0..1/2.
    for (std::size_t v = 4; v < surface.uvs.size() / 2; ++v) {
        CHECK(uv(v).first >= 0.f);
        CHECK(uv(v).first <= 0.25f);
        CHECK(uv(v).second >= 0.f);
        CHECK(uv(v).second <= 0.5f);
    }
    // A region is a fit: the texture is not marked as repeating.
    CHECK_FALSE(result.textures[0].repeats);
}

TEST_CASE("a window outside the image traps") {
    for (const char* window : {"-1.0, 0.0, 4.0, 4.0", "0.0, 0.0, 0.0, 4.0", "60.0, 0.0, 8.0, 4.0", "0.0, 30.0, 4.0, 4.0",
                               "0.0, 0.0, 4.0, 1.0 / 0.0"}) {
        CHECK_THAT(trapMessage(withMaps(mapped(std::string("    s = skin.region(sheet.draw(1), ") + window + ")"))),
                   ContainsSubstring("skin region must lie inside the image"));
    }
    CHECK_THAT(trapMessage(withMaps(mapped("    s = skin.region(sheet.draw(1), 0.0, 0.0, 64.0, 32.0)"))), ContainsSubstring("no trap"));
}

TEST_CASE("a render asset reads an import's constants as alias.name") {
    const auto result = generate(withMaps(mapped(R"(
    s = sheet.draw(1)
    quad({0, 0, 0}, {10, 0, 0}, {10, 5, 0}, {0, 5, 0}, skin.region(s, sheet.frontX, 0.0, sheet.frontW, sheet.rows))
    extrude({0, 0, 2}, sheet.outline, 1.0, s)
    box({0, 0, 6}, {sheet.badge.x, sheet.badge.y, 1}, s)
    side(s)
)", "  side(s: skin) { box({0, 0, 9}, {sheet.frontW / 8.0, 1, 1}, s) }")));
    REQUIRE(result.surfaces.size() == 1);
    const auto& surface = result.surfaces[0];
    CHECK(surface.uvs[0] == 0.25f);
    CHECK(surface.uvs[2] == 0.75f);
    CHECK(surface.uvs[1] == 0.5f);
    const auto badge = generate(withMaps(mapped("    box({0, 0, 6}, {sheet.badge.x, sheet.badge.y, 1}, sheet.draw(1))")));
    CHECK(badge.boundsMax[0] == 3.f);
    CHECK(badge.boundsMax[1] == 4.f);
    // A parameter named like the import is read as the parameter.
    const auto shadowed = generate(withMaps(mapped("    s = sheet.draw(1)\n    side(s, {3, 1})",
                                                   "  side(s: skin, sheet: vec2) { box({0, 0, 0}, {sheet.x, 1, 1}, s) }")));
    CHECK(shadowed.boundsMax[0] == 1.5f);
    // Samplers and distributions come through as well.
    CHECK(generate(withMaps(R"(import sheet from "dice.ips"
asset thing {
  size 40x40x40
  render {
    s = sheet.draw(1)
    box({0, 0, 0}, {1.0 to 3.0 with sheet.sizes, sheet.jitter.pick() + 1.0, 1}, s)
  }
}
)" ), 3).vertexCount == 24);
    // Unknown names, the asset's own constants and draw assets are errors.
    CHECK_THAT(diagnostic(withMaps(mapped("    x = sheet.frontx"))), ContainsSubstring("'sheet' declares no constant 'frontx'"));
    CHECK_THAT(diagnostic(withMaps("import sheet from \"sheet.ips\"\nasset thing {\n  size 4x4x4\n  const f32 half = sheet.frontW / 2.0\n"
                                   "  render { }\n}\n")),
               ContainsSubstring("const 'half': cannot read 'sheet'; only asset constants are allowed"));
    CHECK_THAT(diagnostic({{"model.ips", "import sheet from \"sheet.ips\"\nasset flat {\n  size 8x8\n  draw {\n    x = sheet.frontX\n  }\n}\n"},
                           {"sheet.ips", kSheet}}),
               ContainsSubstring("'sheet.frontX' reads a constant of an import, which only an asset with render { } can do"));
}
