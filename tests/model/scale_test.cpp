// Levels of detail, textures a host already holds and generation from many
// threads (spec models v1.2, M5).
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "support.hpp"

#include <cstring>
#include <stdexcept>
#include <thread>
#include <vector>

using namespace model_support;
using Catch::Matchers::ContainsSubstring;

namespace {

const char* const kGrain = R"(
asset grain {
  size 32x16
  input color tint = #8a5a2b
  output normal relief
  output roughness rough
  draw {
    root = seed.initial()
    rect(0, 0, 32, 16, tint)
    for i in 0..4 { rect(float(i) * 8.0, 0, 7, 16, color.shade(tint, int(roll(root.fork(i), -20.0, 20.0)))) }
    layer height(alpha = surface.source_over) { rect(0, 0, 32, 16, #808080) rect(4, 4, 8, 8, #ffffff) }
    relief = height.normal(2.0)
    layer gloss(alpha = surface.source_over) { rect(0, 0, 32, 16, #404040) }
    rough = gloss
  }
}
)";

// A post whose sides and cap follow the level; its draws do not.
const char* const kPost = R"(
import wood from "grain.ips"
import flat from "plain.ips"
asset post {
  size 16x32x16
  scale 32
  output point3d top
  render {
    root = seed.initial()
    oak = wood.draw(root.fork("oak"))
    grey = flat.draw(root.fork("grey"))
    height = 20.0 to 28.0
    sides = 16 >> model.lod()
    cylinder({0, 0, 0}, 4.0, height, sides, oak)
    if (model.lod() < 2) { sphere({0, height + 2.0, 0}, 3.0, sides, sides / 2, grey) }
    top = {0, height, 0}
  }
}
)";

Sources post() { return {{"model.ips", kPost}, {"grain.ips", kGrain}, {"plain.ips", kPlain}}; }

model::ModelResult at(const model::LoadedModel& loaded, u32 seed, u32 lod, std::span<const model::KnownTexture> known = {}) {
    model::ModelInputs inputs;
    inputs.seed = seed;
    inputs.lod = lod;
    inputs.knownTextures = known;
    return model::generateModel(loaded, inputs);
}

bool sameMesh(const model::ModelResult& a, const model::ModelResult& b) {
    if (a.surfaces.size() != b.surfaces.size() || a.outputs.size() != b.outputs.size()) return false;
    for (std::size_t i = 0; i < a.surfaces.size(); ++i) {
        const auto& x = a.surfaces[i];
        const auto& y = b.surfaces[i];
        const auto bits = [](const std::vector<f32>& u, const std::vector<f32>& v) {
            return u.size() == v.size() && std::memcmp(u.data(), v.data(), u.size() * sizeof(f32)) == 0;
        };
        if (x.texture != y.texture || !bits(x.positions, y.positions) || !bits(x.normals, y.normals) ||
            !bits(x.tangents, y.tangents) || !bits(x.uvs, y.uvs) || x.indices != y.indices ||
            std::bit_cast<u32>(x.roughness) != std::bit_cast<u32>(y.roughness)) return false;
    }
    for (std::size_t i = 0; i < a.outputs.size(); ++i) {
        if (a.outputs[i].points.size() != b.outputs[i].points.size()) return false;
        for (std::size_t k = 0; k < a.outputs[i].points.size(); ++k)
            if (std::memcmp(&a.outputs[i].points[k], &b.outputs[i].points[k], sizeof(Vec3)) != 0) return false;
    }
    return true;
}

} // namespace

TEST_CASE("model.lod reads the level the host asks for", "[scale]") {
    const auto loaded = load(post());
    const auto full = at(loaded, 5, 0), half = at(loaded, 5, 1), coarse = at(loaded, 5, 2);
    CHECK(full.vertexCount > half.vertexCount);
    CHECK(half.vertexCount > coarse.vertexCount);
    CHECK(full.surfaces.size() == 2);
    CHECK(coarse.surfaces.size() == 1); // the cap is left out
    // The level changes no draw: the height and the textures are the same.
    REQUIRE(full.outputs.size() == 1);
    CHECK(std::memcmp(&full.outputs[0].points[0], &coarse.outputs[0].points[0], sizeof(Vec3)) == 0);
    CHECK(full.textures[0].key == coarse.textures[0].key);
    CHECK(full.textures[0].color.pixels == coarse.textures[0].color.pixels);
    // The default is full detail, and a level beyond 7 is an argument error.
    CHECK(sameMesh(model::generateModel(loaded, {}), at(loaded, 0, 0)));
    try {
        (void)at(loaded, 5, 8);
        FAIL("level 8 was accepted");
    } catch (const FormatError& error) {
        CHECK(error.stage == ErrorStage::Arguments);
        CHECK_THAT(error.what(), ContainsSubstring("model lod must be in 0..7"));
    }
}

TEST_CASE("a part reads the level of its model", "[scale]") {
    const Sources sources{
        {"model.ips", R"(
import post from "post.ips"
import flat from "plain.ips"
asset pair {
  size 40x32x16
  scale 32
  output point3d tip
  render {
    place(at = {-10, 0, 0}) { post.render(seed.initial().fork("left")) }
    place(at = {10, 0, 0}) {
      post.render(seed.initial().fork("right"))
      tip = post.outputs.top
    }
    box({0, 0, 0}, {4, 2, 4}, flat.draw(1))
  }
}
)"},
        {"post.ips", kPost}, {"grain.ips", kGrain}, {"plain.ips", kPlain}};
    const auto loaded = load(sources);
    const auto full = at(loaded, 5, 0), coarse = at(loaded, 5, 2);
    // Two posts of 16 sides with caps, then two of 4 sides without.
    CHECK(full.vertexCount > coarse.vertexCount);
    const auto alone = load(post());
    CHECK(full.triangleCount - coarse.triangleCount == 2 * (at(alone, 5, 0).triangleCount - at(alone, 5, 2).triangleCount));
    // The part's output is the same point at every level.
    REQUIRE(full.outputs.size() == 1);
    CHECK(std::memcmp(&full.outputs[0].points[0], &coarse.outputs[0].points[0], sizeof(Vec3)) == 0);
}

TEST_CASE("model.lod is a render builtin only", "[scale]") {
    // The level is known when the model is generated, not when it is compiled.
    CHECK_THAT(diagnostic({{"model.ips", R"(import flat from "plain.ips"
asset a { size 8x8x8 scale 32 const i32 level = model.lod() render { box({0, 0, 0}, {1, 1, 1}, flat.draw(1)) } })"},
        {"plain.ips", kPlain}}), ContainsSubstring("const"));
    CHECK_THAT(diagnostic({{"model.ips", "asset a { size 8x8 draw { x = model.lod() } }"}}), ContainsSubstring("model"));
    CHECK_THAT(diagnostic({{"model.ips", R"(import flat from "plain.ips"
asset a { size 8x8x8 scale 32 render { x = model.lod(1) } })"}, {"plain.ips", kPlain}}), ContainsSubstring("model.lod"));
}

TEST_CASE("a texture the host holds is charged and not generated", "[scale]") {
    const auto loaded = load(post());
    const auto first = at(loaded, 9, 0);
    REQUIRE(first.textures.size() == 2);
    for (const auto& texture : first.textures) {
        CHECK_FALSE(texture.reused);
        CHECK(texture.fuel > 0);
        CHECK(texture.work > 0);
    }
    // The wood has a normal and a roughness map, the plain skin none.
    CHECK(first.textures[0].kinds == ((1u << 1) | (1u << 2)));
    CHECK(first.textures[1].kinds == 0);

    std::vector<model::KnownTexture> known;
    for (const auto& texture : first.textures) known.push_back(model::knownTexture(texture));
    const auto again = at(loaded, 9, 0, known);
    CHECK(sameMesh(first, again));
    CHECK(again.fuelUsed == first.fuelUsed);
    CHECK(again.workUsed == first.workUsed);
    REQUIRE(again.textures.size() == 2);
    for (std::size_t i = 0; i < 2; ++i) {
        const auto& texture = again.textures[i];
        CHECK(texture.reused);
        CHECK(texture.key == first.textures[i].key);
        CHECK(texture.color.pixels.empty());
        CHECK(texture.images.empty());
        CHECK(texture.color.width == first.textures[i].color.width);
        CHECK(texture.kinds == first.textures[i].kinds);
        CHECK(texture.fuel == first.textures[i].fuel);
        CHECK(texture.repeats == first.textures[i].repeats);
    }
    // Another level shares the textures; another seed has its own.
    const auto coarse = at(loaded, 9, 2, known);
    CHECK(coarse.textures[0].reused);
    const auto other = at(loaded, 10, 0, known);
    CHECK_FALSE(other.textures[0].reused);
    // One known texture of two.
    const auto mixed = at(loaded, 9, 0, std::span(known).first(1));
    CHECK(mixed.textures[0].reused);
    CHECK_FALSE(mixed.textures[1].reused);
    CHECK(mixed.fuelUsed == first.fuelUsed);
}

TEST_CASE("a budget too small fails alike with and without the texture", "[scale]") {
    auto loaded = load(post());
    const auto first = at(loaded, 9, 0);
    std::vector<model::KnownTexture> known;
    for (const auto& texture : first.textures) known.push_back(model::knownTexture(texture));
    const auto reason = [&](const model::ModelInputs& inputs) {
        try {
            (void)model::generateModel(loaded, inputs);
        } catch (const FormatError& error) {
            return std::pair(error.reason, std::string(error.what()));
        }
        return std::pair(ErrorReason::None, std::string());
    };
    model::ModelInputs plain, cached;
    plain.seed = cached.seed = 9;
    cached.knownTextures = known;

    // Work: enough for neither, then exactly enough for both.
    plain.limits.workLimit = cached.limits.workLimit = first.textures[0].work / 2;
    const auto slow = reason(plain);
    CHECK(slow.first != ErrorReason::None);
    CHECK(reason(cached).first == slow.first);
    plain.limits.workLimit = cached.limits.workLimit = first.workUsed;
    CHECK(reason(plain).first == ErrorReason::None);
    CHECK(reason(cached).first == ErrorReason::None);

    // Fuel: the model's budget stops inside the first texture.
    plain.limits = cached.limits = {};
    loaded.file.meta.fuel = first.textures[0].fuel / 2;
    const auto dry = reason(plain);
    CHECK(dry.first == ErrorReason::FuelExhausted);
    CHECK_THAT(dry.second, ContainsSubstring("skin 'wood'"));
    const auto dryCached = reason(cached);
    CHECK(dryCached.first == dry.first);
    CHECK_THAT(dryCached.second, ContainsSubstring("skin 'wood'"));
    loaded.file.meta.fuel = first.fuelUsed;
    CHECK(reason(plain).first == ErrorReason::None);
    CHECK(reason(cached).first == ErrorReason::None);
}

TEST_CASE("a texture survives its file and a store completes a result", "[scale]") {
    const auto loaded = load(post());
    const auto first = at(loaded, 9, 0);
    const auto bytes = model::packTexture(first.textures[0]);
    CHECK(model::textureFileName(0x0123456789abcdefull) == "0123456789abcdef.iptx");
    const auto back = model::unpackTexture(bytes);
    REQUIRE(back);
    CHECK(back->key == first.textures[0].key);
    CHECK(back->name == "wood");
    CHECK(back->fuel == first.textures[0].fuel);
    CHECK(back->work == first.textures[0].work);
    CHECK(back->kinds == first.textures[0].kinds);
    CHECK(back->color.pixels == first.textures[0].color.pixels);
    REQUIRE(back->images.size() == 2);
    CHECK(back->images[1].kind == AuxImageKind::Roughness);
    CHECK(back->images[1].image.pixels == first.textures[0].images[1].image.pixels);

    // The record is in the first bytes of the file.
    static_assert(model::kTextureHeaderBytes == 64);
    const auto peeked = model::peekTexture(std::span(bytes).first(model::kTextureHeaderBytes));
    REQUIRE(peeked);
    CHECK(peeked->key == first.textures[0].key);
    CHECK(peeked->fuel == first.textures[0].fuel);
    CHECK(peeked->work == first.textures[0].work);
    CHECK(peeked->kinds == first.textures[0].kinds);
    CHECK_FALSE(model::peekTexture(std::span(bytes).first(63)));

    // Another stamp is a miss, damage an error.
    auto stale = bytes;
    stale[8] ^= 1;
    CHECK_FALSE(model::unpackTexture(stale));
    CHECK_FALSE(model::peekTexture(std::span(stale).first(model::kTextureHeaderBytes)));
    auto damaged = bytes;
    damaged[damaged.size() / 2] ^= 0x40;
    CHECK_THROWS_WITH(model::unpackTexture(damaged), ContainsSubstring("checksum"));
    CHECK_THROWS_WITH(model::unpackTexture(std::span(bytes).first(bytes.size() - 9)), ContainsSubstring("stored texture"));
    CHECK_THROWS_AS(model::unpackTexture(std::span(bytes).first(3)), FormatError);

    model::TextureStore store;
    CHECK(store.add(*back));
    CHECK_FALSE(store.add(*back));
    auto next = at(loaded, 9, 1, store.known());
    CHECK(next.textures[0].reused);
    CHECK_FALSE(next.textures[1].reused);
    CHECK_THROWS_AS(model::packTexture(next.textures[0]), std::invalid_argument);
    store.complete(next);
    CHECK(store.size() == 2);
    CHECK_FALSE(next.textures[0].reused);
    CHECK(next.textures[0].color.pixels == first.textures[0].color.pixels);
    CHECK(next.textures[0].images.size() == 2);
    auto third = at(loaded, 9, 2, store.known());
    CHECK(third.textures[0].reused);
    model::TextureStore empty;
    CHECK_THROWS_AS(empty.complete(third), std::logic_error);
}

TEST_CASE("one loaded model generates from many threads at once", "[scale]") {
    const auto loaded = load(post());
    constexpr u32 kThreads = 8, kRounds = 6;
    std::vector<model::ModelResult> serial;
    for (u32 i = 0; i < kThreads * kRounds; ++i) serial.push_back(at(loaded, 100 + i, i % 3));
    std::vector<model::ModelResult> parallel(serial.size());
    std::vector<std::string> failures(kThreads);
    std::vector<std::thread> threads;
    for (u32 t = 0; t < kThreads; ++t)
        threads.emplace_back([&, t] {
            try {
                for (u32 round = 0; round < kRounds; ++round) {
                    const u32 i = round * kThreads + t;
                    parallel[i] = at(loaded, 100 + i, i % 3);
                }
            } catch (const std::exception& error) {
                failures[t] = error.what();
            }
        });
    for (auto& thread : threads) thread.join();
    for (const auto& failure : failures) CHECK(failure.empty());
    for (std::size_t i = 0; i < serial.size(); ++i) {
        INFO("generation " << i);
        CHECK(sameMesh(serial[i], parallel[i]));
        CHECK(serial[i].fuelUsed == parallel[i].fuelUsed);
        CHECK(serial[i].workUsed == parallel[i].workUsed);
        REQUIRE(serial[i].textures.size() == parallel[i].textures.size());
        for (std::size_t k = 0; k < serial[i].textures.size(); ++k)
            CHECK(serial[i].textures[k].color.pixels == parallel[i].textures[k].color.pixels);
    }
}
