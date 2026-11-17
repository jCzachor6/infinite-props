// Shaping blocks end to end (models v1.2, M3): smooth, refine, bend, twist,
// taper and displace compiled from source, their diagnostics, their traps,
// and what leaves a block early.
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "model/support.hpp"

using namespace model_support;
using Catch::Matchers::ContainsSubstring;

namespace {

u32 vertices(const model::ModelResult& result) { return result.vertexCount; }

} // namespace

TEST_CASE("shaping blocks compile and change the shape") {
    const auto plain = generate(with(thing("    cylinder({0, 0, 0}, 2.0, 20.0, 8, s)")));
    const auto refined = generate(with(thing("    refine(3) { cylinder({0, 0, 0}, 2.0, 20.0, 8, s) }")));
    CHECK(refined.triangleCount == plain.triangleCount * 9);
    const auto bent = generate(with(thing("    bend(90.0) { refine(4) { cylinder({0, 0, 0}, 2.0, 20.0, 8, s) } }")));
    CHECK(bent.boundsMax[0] > 10.f);
    const auto rock = generate(with(thing(R"(
    smooth(50.0) {
      displace(1.5, 0.2, seed.initial().fork("rock")) { refine(4) { box({0, 0, 0}, {10, 10, 10}, s) } }
    })")));
    CHECK(rock.vertexCount > plain.vertexCount);
    // The same seed builds the same rock.
    const auto again = generate(with(thing(R"(
    smooth(50.0) {
      displace(1.5, 0.2, seed.initial().fork("rock")) { refine(4) { box({0, 0, 0}, {10, 10, 10}, s) } }
    })")));
    CHECK(rock.surfaces[0].positions == again.surfaces[0].positions);
}

TEST_CASE("a point in a bend is where a vertex built there is") {
    const auto result = generate(with(thing(R"(
    place(at = {5, 1, -2}, yaw = 20.0) {
      bend(75.0) {
        tri({0.5, 12.25, 0.75}, {1, 0, 0}, {0, 1, 0}, s)
        box({0, 0, 0}, {1, 20, 1}, s)
        tip = {0.5, 12.25, 0.75}
      }
    })", "  output point3d tip")));
    const auto& tip = output(result, "tip");
    REQUIRE(tip.points.size() == 1);
    const auto& positions = result.surfaces[0].positions;
    CHECK(bits(tip.points[0].x) == bits(positions[0]));
    CHECK(bits(tip.points[0].y) == bits(positions[1]));
    CHECK(bits(tip.points[0].z) == bits(positions[2]));
}

TEST_CASE("the checker knows the shaping blocks") {
    CHECK_THAT(diagnostic({{"model.ips", "asset flat {\n  size 8x8\n  draw { smooth(30.0) { } }\n}\n"}}),
               ContainsSubstring("smooth shapes 3D geometry; it is available in a render asset"));
    CHECK_THAT(diagnostic(with(thing("    smooth() { }"))), ContainsSubstring("smooth takes 1 argument (angle)"));
    CHECK_THAT(diagnostic(with(thing("    displace(1.0, 2.0) { }"))),
               ContainsSubstring("displace takes 3 arguments (amount, frequency, seed)"));
    CHECK_THAT(diagnostic(with(thing("    bend(angle = 30.0) { }"))), ContainsSubstring("takes its arguments in order"));
    CHECK_THAT(diagnostic(with(thing("    refine(2.5) { }"))), ContainsSubstring("'count' of refine must be an i32"));
    CHECK_THAT(diagnostic(with(thing("    taper({1, 2, 3}) { }"))), ContainsSubstring("'scale' of taper must be a number"));
    CHECK_THAT(diagnostic(with(thing("    displace(1.0, 0.5, #fff) { }"))), ContainsSubstring("must be a seed"));
    // A box cannot bend; a point can, and smooth leaves both alone.
    CHECK_THAT(diagnostic(with(thing("    bend(30.0) { body = {{0, 0, 0}, {1, 1, 1}} }", "  output box3d body"))),
               ContainsSubstring("a box3d output cannot be written inside bend, twist or taper"));
    CHECK(compile(with(thing("    smooth(30.0) { body = {{0, 0, 0}, {1, 1, 1}} }\n    twist(30.0) { tip = {1, 1, 1} }",
                             "  output box3d body\n  output point3d tip"))).succeeded());
    // Without a block, the names are ordinary helpers.
    CHECK(compile(with(thing("    smooth(1.0)", "", "  smooth(x: f32) { }"))).succeeded() == false);
    // Blocks and places share the limit of 32 scopes.
    std::string deep;
    for (int i = 0; i < 33; ++i) deep += "smooth(1.0) {\n";
    for (int i = 0; i < 33; ++i) deep += "}\n";
    CHECK_THAT(diagnostic(with(thing(deep))), ContainsSubstring("nest deeper than 32"));
}

TEST_CASE("shaping blocks check their arguments when they run") {
    const auto trap = [](const std::string& body) { return trapMessage(with(thing(body))); };
    CHECK_THAT(trap("    a = 200.0\n    smooth(a) { }"), ContainsSubstring("smooth angle must be in 0..180"));
    CHECK_THAT(trap("    n = 17\n    refine(n) { }"), ContainsSubstring("refine count must be in 1..16"));
    CHECK_THAT(trap("    a = 400.0\n    bend(a) { }"), ContainsSubstring("bend angle must be in -360..360"));
    CHECK_THAT(trap("    k = 0.0\n    taper(k) { }"), ContainsSubstring("taper scale must be finite and positive"));
    CHECK_THAT(trap("    f = 0.0\n    displace(1.0, f, 1) { }"), ContainsSubstring("displace needs a finite amount"));
    CHECK_THAT(trap("    bend(360.0) { box({30, 0, 0}, {20, 10, 1}, s) }"), ContainsSubstring("bend is too tight"));
    // A helper that writes a box from inside a bend traps.
    CHECK_THAT(trapMessage(with(thing("    bend(30.0) { mark() }", "  output box3d body",
                                      "  mark() { body = {{0, 0, 0}, {1, 1, 1}} }"))),
               ContainsSubstring("a box3d output cannot be written inside bend, twist or taper"));
}

TEST_CASE("a block whose argument changes nothing leaves its contents bit for bit") {
    // Curved and flat primitives, at the root and in a turned, scaled and
    // mirrored place: the block opens a buffer of its own and passes it on,
    // and that must not move a bit. smooth(1.0) joins only the copies of a
    // normal on the seams of the images, which changes nothing either.
    const std::string body = "    cylinder({1, 0, 2}, 2.0, 7.0, 7, s)\n"
                             "    box({-3, 1, 0}, {2, 3, 4}, 0.4, s)\n"
                             "    sphere({4, 6, -1}, 1.5, 9, 5, s)\n"
                             "    tube({0, 0, 0}, {0, 5, 0}, {4, 6, 1}, {6, 2, -2}, 0.8, 0.4, 7, 9, s)\n";
    const auto bitsOf = [](const std::vector<f32>& values) {
        std::vector<u32> out;
        for (const f32 value : values) out.push_back(bits(value));
        return out;
    };
    const std::vector<std::string> scopes{"", "place(at = {2, 1, -3}, yaw = 33.0, pitch = -17.0, roll = 8.0, scale = 1.5, "
                                              "mirror_x = 1)"};
    const std::vector<std::string> blocks{"bend(0.0)", "twist(0.0)", "taper(1.0)", "refine(1)", "smooth(0.0)", "smooth(1.0)",
                                          "displace(0.0, 0.2, seed.initial().fork(\"none\"))"};
    for (const std::string& scope : scopes) {
        const auto wrap = [&](const std::string& inner) { return scope.empty() ? inner : "    " + scope + " {\n" + inner + "    }\n"; };
        const auto plain = generate(with(thing(wrap(body))));
        for (const std::string& block : blocks) {
            INFO(block << " in '" << scope << "'");
            const auto shaped = generate(with(thing(wrap("    " + block + " {\n" + body + "    }\n"))));
            REQUIRE(shaped.surfaces.size() == plain.surfaces.size());
            for (std::size_t s = 0; s < plain.surfaces.size(); ++s) {
                CHECK(bitsOf(shaped.surfaces[s].positions) == bitsOf(plain.surfaces[s].positions));
                CHECK(bitsOf(shaped.surfaces[s].normals) == bitsOf(plain.surfaces[s].normals));
                CHECK(bitsOf(shaped.surfaces[s].tangents) == bitsOf(plain.surfaces[s].tangents));
                CHECK(bitsOf(shaped.surfaces[s].uvs) == bitsOf(plain.surfaces[s].uvs));
                CHECK(shaped.surfaces[s].indices == plain.surfaces[s].indices);
            }
        }
    }
}

TEST_CASE("leaving a block early still passes what it built") {
    // break out of a loop, and return out of a helper, from inside blocks.
    const auto broken = generate(with(thing(R"(
    for i in 0..4 {
      smooth(30.0) {
        refine(2) {
          box({float(i) * 3.0, 0, 0}, {1, 1, 1}, s)
          if (i == 1) { break }
        }
      }
    })")));
    // Two boxes, each refined: 24 vertices and one more on each of 30 edges.
    CHECK(vertices(broken) == 2 * (24 + 30));
    const auto returned = generate(with(thing("    twist(45.0) { piece(s) }\n    box({5, 0, 0}, {1, 1, 1}, s)", "",
                                              "  piece(k: skin) {\n    taper(0.5) {\n      box({0, 0, 0}, {1, 4, 1}, k)\n"
                                              "      return\n    }\n  }")));
    CHECK(vertices(returned) == 48);
}

TEST_CASE("the M3 tariff: blocks, refine, paths, parts and outputs") {
    // The prices, pinned (benchmarks/operations/README.md, milestone M3).
    CHECK(model::kBlockFuel == 24);
    CHECK(model::kPassVertexFuel == 3);
    CHECK(model::kPassTriangleFuel == 1);
    CHECK(model::kSmoothFuel == 8);
    CHECK(model::kDeformFuel == 3);
    CHECK(model::kDisplaceFuel == 18);
    CHECK(model::kDisplaceTriangleFuel == 4);
    CHECK(model::kRefineVertexFuel == 5);
    CHECK(model::kRefineTriangleFuel == 2);
    CHECK(model::kPathPointFuel == 24);
    CHECK(model::kPointOutputFuel == 8);
    CHECK(model::kBoxOutputFuel == 16);

    const auto fuel = [](const std::string& body, const std::string& declarations = "") {
        return generate(with(thing(body, declarations))).fuelUsed;
    };
    const std::string box = "box({0, 0, 0}, {2, 3, 4}, s)";
    const u64 plain = fuel("    " + box);
    // A block: its argument, its opening with the reserve and its end, then
    // the box's 24 vertices and 12 triangles handed on.
    const u64 open = 1 + (1 + model::kBlockFuel) + 1;
    const u64 handed = 24 * model::kPassVertexFuel + 12 * model::kPassTriangleFuel;
    CHECK(fuel("    smooth(0.0) { " + box + " }") - plain == open + 24 * model::kSmoothFuel + handed);
    CHECK(fuel("    bend(30.0) { " + box + " }") - plain == open + 24 * model::kDeformFuel + handed);
    CHECK(fuel("    taper(1.0) { " + box + " }") - plain == open + 24 * model::kDeformFuel + handed);
    // refine(2) adds a vertex on each of 30 edges and makes 48 triangles of 12.
    CHECK(fuel("    refine(2) { " + box + " }") - plain ==
          open + 30 * model::kRefineVertexFuel + 36 * model::kRefineTriangleFuel +
              54 * model::kPassVertexFuel + 48 * model::kPassTriangleFuel);
    // displace has three arguments.
    CHECK(fuel("    displace(1.0, 0.5, 3) { " + box + " }") - plain ==
          open + 2 + 24 * model::kDisplaceFuel + 12 * model::kDisplaceTriangleFuel + handed);
    // A sweep pays for each point of its path.
    const std::string profile = "[{-1, -1}, {1, -1}, {1, 1}, {-1, 1}]";
    const std::string paths = "    a = [{0, 0, 0}, {0, 5, 0}]\n    b = [{0, 0, 0}, {0, 5, 0}, {5, 5, 0}]\n";
    const u64 two = fuel(paths + "    sweep(a, " + profile + ", s)");
    const u64 three = fuel(paths + "    sweep(b, " + profile + ", s)");
    CHECK(three - two == model::kPathPointFuel + 16 * model::kVertexFuel + 8 * model::kTriangleFuel);
    // An output write: its value, the instruction and the reserve.
    const u64 none = fuel("    v = {1, 2, 3}", "  output point3d p\n  output box3d b");
    CHECK(fuel("    v = {1, 2, 3}\n    p = v", "  output point3d p\n  output box3d b") - none == 2 + model::kPointOutputFuel);
    CHECK(fuel("    v = {1, 2, 3}\n    b = {v, v}", "  output point3d p\n  output box3d b") - none == 3 + model::kBoxOutputFuel);
}
