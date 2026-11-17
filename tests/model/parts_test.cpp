// 3D parts (models v1.2, M3): a render asset builds another where it calls
// it, in its own stream, with its own skins, and reads the part's points in
// the block of the call.
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "model/support.hpp"

using namespace model_support;
using Catch::Matchers::ContainsSubstring;

namespace {

const char* const kPeg = R"(
import flat from "plain.ips"
asset peg {
  size 4x10x4
  input f32 tall = 10.0
  output point3d tip
  output point3d never
  output list box3d colliders
  render {
    s = flat.draw(seed.initial())
    w = 1.0 to 2.0
    tri({0.25, 0.5, 0.75}, {1, 0, 0}, {0, 1, 0}, s)
    box({0, 0, 0}, {w, tall, w}, s)
    tip = {0.25, 0.5, 0.75}
    colliders = {{0, 0, 0}, {w, tall, w}}
  }
}
)";

Sources withPeg(const std::string& model) { return {{"model.ips", model}, {"plain.ips", kPlain}, {"peg.ips", kPeg}}; }

std::string board(const std::string& body, const std::string& declarations = "") {
    return "import peg from \"peg.ips\"\nimport flat from \"plain.ips\"\nasset board {\n  size 40x40x40\n" +
           declarations + "\n  render {\n    s = flat.draw(1)\n" + body + "\n  }\n}\n";
}

} // namespace

TEST_CASE("a part is built where it is called, and its point read there") {
    const auto result = generate(withPeg(board(R"(
    place(at = {3, 1, 2}, yaw = 30.0, mirror_x = 1) {
      peg.render(seed.initial().fork("a"), tall = 6.0)
      tip = peg.outputs.tip
    })", "  output point3d tip")));
    // The part's first vertex (its tri) and the point it wrote, carried
    // through the parent's block, have the same bits.
    const model::ModelSurface* surface = nullptr;
    for (const auto& candidate : result.surfaces)
        if (candidate.positions.size() == 3 * (3 + 24)) surface = &candidate;
    REQUIRE(surface != nullptr);
    const auto& point = output(result, "tip").points.at(0);
    CHECK(bits(point.x) == bits(surface->positions[0]));
    CHECK(bits(point.y) == bits(surface->positions[1]));
    CHECK(bits(point.z) == bits(surface->positions[2]));
    // Parts' outputs never reach the host by themselves.
    CHECK(result.outputs.size() == 1);
}

TEST_CASE("each call of a part has its own stream and its own outputs") {
    const auto result = generate(withPeg(board(R"(
    for i in 0..3 {
      place(at = {float(i) * 10.0, 0, 0}) {
        peg.render(seed.initial().fork(i))
        tips = peg.outputs.tip
      }
    }
    a = 1 to 1000
    after = {float(a), 0, 0}
)", "  output list point3d tips\n  output point3d after")));
    const auto& tips = output(result, "tips");
    REQUIRE(tips.points.size() == 3);
    CHECK(tips.points[1].x == 10.25f);
    CHECK(tips.points[2].x == 20.25f);
    // The calls drew from their own streams: the model's next draw is the
    // one it would have drawn without them.
    const auto alone = generate(withPeg(board("    a = 1 to 1000\n    after = {float(a), 0, 0}", "  output point3d after")));
    CHECK(output(result, "after").points[0].x == output(alone, "after").points[0].x);
    // Three calls with three seeds: three different widths, three textures
    // of the part's skin.
    CHECK(result.textures.size() == 4);
}

TEST_CASE("a part's outputs are read right after its call, in its block") {
    CHECK_THAT(trapMessage(withPeg(board(R"(
    place(at = {1, 0, 0}) { peg.render(1) }
    tip = peg.outputs.tip
)", "  output point3d tip"))), ContainsSubstring("a part's outputs are read in the block that called it"));
    CHECK_THAT(trapMessage(withPeg(board("    tip = peg.outputs.tip", "  output point3d tip"))),
               ContainsSubstring("slot does not belong to the last called part"));
    CHECK_THAT(trapMessage(withPeg(board("    peg.render(1)\n    tip = peg.outputs.never", "  output point3d tip"))),
               ContainsSubstring("part output not written"));
    // Lists and boxes of a part stay in the part.
    CHECK_THAT(diagnostic(withPeg(board("    peg.render(1)\n    b = peg.outputs.colliders"))),
               ContainsSubstring("cannot read list output 'colliders'"));
}

TEST_CASE("parts follow the blocks they are called in") {
    const auto bent = generate(withPeg(board(R"(
    bend(90.0) {
      refine(4) { box({0, 0, 0}, {1, 20, 1}, s) }
      place(at = {0, 20, 0}) {
        peg.render(1)
        tip = peg.outputs.tip
      }
    })", "  output point3d tip")));
    // The block is 30 units tall (box and peg): the peg's tip lies on the bend.
    CHECK(output(bent, "tip").points.at(0).x > 5.f);
}

TEST_CASE("the compiler checks parts") {
    // Units must agree.
    const std::string big = "import flat from \"plain.ips\"\nasset big {\n  size 4x4x4\n  scale 32\n  render { box({0, 0, 0}, {1, 1, 1}, flat.draw(1)) }\n}\n";
    CHECK_THAT(diagnostic({{"model.ips", "import big from \"big.ips\"\nasset m {\n  size 4x4x4\n  render { big.render(1) }\n}\n"},
                           {"big.ips", big}, {"plain.ips", kPlain}}),
               ContainsSubstring("part 'big' declares scale 32 and the model 100; give both the same scale, or call the part in place(scale = 3.125)"));
    CHECK_THAT(diagnostic(withPeg(board("    q = peg.draw(1)"))), ContainsSubstring("'peg' is a render asset; build it with peg.render"));
    CHECK_THAT(diagnostic(withPeg(board("    flat.render(1)"))), ContainsSubstring("'flat' has draw { }: use flat.draw"));
    CHECK_THAT(diagnostic(withPeg(board("    peg.render(1, at = {1, 2, 3})"))), ContainsSubstring("put the call in a place(...) block"));
    CHECK_THAT(diagnostic(withPeg(board("    peg.render(1, tall = #fff)"))), ContainsSubstring("input 'tall' of part 'peg' must be f32"));
    CHECK_THAT(diagnostic({{"model.ips", "import peg from \"peg.ips\"\nasset flat2 {\n  size 8x8\n  draw { }\n}\n"},
                           {"peg.ips", kPeg}, {"plain.ips", kPlain}}),
               ContainsSubstring("a draw asset cannot use a 3D model as a part"));
}

TEST_CASE("parts nest, and share textures by key") {
    const std::string pair = R"(
import peg from "peg.ips"
import flat from "plain.ips"
asset pair {
  size 8x10x4
  output point3d tip
  render {
    s = flat.draw(1)
    place(at = {-2, 0, 0}) { peg.render(seed.initial()) }
    place(at = {2, 0, 0}) {
      peg.render(seed.initial())
      tip = peg.outputs.tip
    }
  }
}
)";
    const auto result = generate({{"model.ips", board(R"(
    pair.render(5)
    tip = pair.outputs.tip
)", "  output point3d tip").replace(0, 25, "import pair from \"pair.ips\"")},
                                  {"pair.ips", pair}, {"peg.ips", kPeg}, {"plain.ips", kPlain}});
    CHECK(output(result, "tip").points.at(0).x == 2.25f);
    // Two pegs from one seed share their texture; the board's and the
    // pair's flat skins with seed 1 are one texture as well.
    CHECK(result.textures.size() == 2);
    const auto loaded = load({{"model.ips", board("    pair.render(5)").replace(0, 25, "import pair from \"pair.ips\"")},
                              {"pair.ips", pair}, {"peg.ips", kPeg}, {"plain.ips", kPlain}});
    // One instance per import, however often it is called.
    REQUIRE(loaded.parts.size() == 2);
    CHECK(loaded.parts[0].name == "pair");
    CHECK(loaded.parts[1].name == "pair.peg");
    CHECK(loaded.parts[1].parent == 0);
}
