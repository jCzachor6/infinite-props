// 3D outputs (models v1.2, M3): points and boxes a model hands its host. A
// value written inside a block moves, turns, scales and mirrors with the
// block, exactly as a vertex built there does.
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "model/support.hpp"

#include <cmath>

using namespace model_support;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::WithinAbs;

namespace {

// The first vertex of the first surface: the first corner of a tri built
// before anything else.
std::array<u32, 3> firstVertex(const model::ModelResult& result) {
    REQUIRE_FALSE(result.surfaces.empty());
    const auto& positions = result.surfaces[0].positions;
    REQUIRE(positions.size() >= 3);
    return {bits(positions[0]), bits(positions[1]), bits(positions[2])};
}

std::array<u32, 3> pointBits(const model::ModelOutput& output, std::size_t index = 0) {
    REQUIRE(output.points.size() > index);
    const Vec3 point = output.points[index];
    return {bits(point.x), bits(point.y), bits(point.z)};
}

} // namespace

TEST_CASE("a point written in a block is where a vertex built there is") {
    // Nested turns, an unequal scale and a mirror: the point and the first
    // corner of the tri go through the same scopes, bit for bit.
    const std::string body = R"(
    place(at = {3.5, 7.25, -2.0}, yaw = 33.0, pitch = -12.5) {
      place(scale = {1.5, 0.75, 2.0}, mirror_x = 1) {
        place(roll = 71.0) {
          tri({1.25, 2.5, 3.75}, {2, 0, 0}, {0, 2, 0}, s)
          tip = {1.25, 2.5, 3.75}
        }
      }
    })";
    const auto direct = generate(with(thing(body, "  output point3d tip")));
    CHECK(pointBits(output(direct, "tip")) == firstVertex(direct));

    // The same written by a helper called in the block.
    std::string helper = body;
    helper.replace(helper.find("tip = {1.25, 2.5, 3.75}"), 23, "mark({1.25, 2.5, 3.75})");
    const auto viaHelper = generate(with(thing(helper, "  output point3d tip", "  mark(p: vec3) { tip = p }")));
    CHECK(pointBits(output(viaHelper, "tip")) == firstVertex(viaHelper));
    CHECK(firstVertex(viaHelper) == firstVertex(direct));
}

TEST_CASE("singular outputs keep the last value, lists every value in order") {
    const auto result = generate(with(thing(R"(
    tip = {1, 2, 3}
    place(at = {10, 0, 0}) {
      tip = {0, 0, 0}
      marks = {0, 1, 0}
    }
    marks = {0, 0, 5}
    for i in 0..3 { marks = {float(i), 0, 0} }
)", "  output point3d tip\n  output list point3d marks\n  output point3d never\n  output list box3d none")));
    const auto& tip = output(result, "tip");
    CHECK_FALSE(tip.list);
    REQUIRE(tip.points.size() == 1);
    CHECK(tip.points[0].x == 10.f);
    const auto& marks = output(result, "marks");
    CHECK(marks.list);
    REQUIRE(marks.points.size() == 5);
    CHECK(marks.points[0].x == 10.f);
    CHECK(marks.points[0].y == 1.f);
    CHECK(marks.points[1].z == 5.f);
    CHECK(marks.points[4].x == 2.f);
    CHECK(output(result, "never").points.empty());
    CHECK(output(result, "none").boxes.empty());
    // Declaration order, the kinds and the list flags reach the host.
    REQUIRE(result.outputs.size() == 4);
    CHECK(result.outputs[0].name == "tip");
    CHECK(result.outputs[3].kind == model::ModelOutputKind::Box3d);
}

TEST_CASE("a box keeps its shape: centre, size and axes that form a rotation") {
    const auto result = generate(with(thing(R"(
    body = {{0, 0, 0}, {10, 60, 10}}
    place(at = {0, 60, 0}, yaw = 30.0, mirror_z = 1) {
      place(scale = 2.0) { boxes = {{1, 0, 0}, {20, 2, 4}} }
    }
    place(scale = {2, 1, 1}, yaw = 90.0) { boxes = {{0, 0, 0}, {1, 2, 3}} }
)", "  output box3d body\n  output list box3d boxes")));
    const auto& body = output(result, "body");
    REQUIRE(body.boxes.size() == 1);
    CHECK(body.boxes[0].center[1] == 30.f);
    CHECK(body.boxes[0].size[1] == 60.f);
    const auto& boxes = output(result, "boxes");
    REQUIRE(boxes.boxes.size() == 2);
    const auto& turned = boxes.boxes[0];
    CHECK_THAT(turned.size[0], WithinAbs(40.0, 1e-4));
    CHECK_THAT(turned.size[1], WithinAbs(4.0, 1e-5));
    CHECK_THAT(turned.center[1], WithinAbs(62.0, 1e-5));
    // The axes are unit, at right angles and right-handed, mirror or not.
    for (const auto& box : boxes.boxes) {
        const auto& a = box.axes;
        const double det = a[0][0] * (a[1][1] * a[2][2] - a[1][2] * a[2][1]) -
                           a[0][1] * (a[1][0] * a[2][2] - a[1][2] * a[2][0]) +
                           a[0][2] * (a[1][0] * a[2][1] - a[1][1] * a[2][0]);
        CHECK_THAT(det, WithinAbs(1.0, 1e-6));
        for (int i = 0; i < 3; ++i)
            CHECK_THAT(std::sqrt(a[i][0] * a[i][0] + a[i][1] * a[i][1] + a[i][2] * a[i][2]), WithinAbs(1.0, 1e-6));
    }
    // A scale inside a quarter turn stays a box: the scale applies along
    // the turned axes.
    CHECK_THAT(boxes.boxes[1].size[0], WithinAbs(2.0, 1e-6));
    CHECK_THAT(boxes.boxes[1].size[2], WithinAbs(3.0, 1e-6));
}

TEST_CASE("a box turned inside an unequal scale traps; a point does not") {
    CHECK_THAT(trapMessage(with(thing(R"(
    place(scale = {2, 1, 1}) { place(yaw = 30.0) { body = {{0, 0, 0}, {1, 1, 1}} } }
)", "  output box3d body"))), ContainsSubstring("a box3d output cannot be turned inside an unequal scale"));
    // A quarter turn, or a turn outside the scale, keeps the box a box.
    CHECK(trapMessage(with(thing(R"(
    place(scale = {2, 1, 1}) { place(yaw = 90.0) { body = {{0, 0, 0}, {1, 1, 1}} } }
    place(yaw = 30.0) { place(scale = {2, 1, 1}) { body = {{0, 0, 0}, {1, 1, 1}} } }
    place(scale = {2, 1, 1}) { place(yaw = 30.0) { tip = {1, 1, 1} } }
)", "  output box3d body\n  output point3d tip"))) == "no trap");
    CHECK_THAT(trapMessage(with(thing(R"(
    body = {{0, 0, 0}, {1, 0, 1}}
)", "  output box3d body"))), ContainsSubstring("positive size"));
}

TEST_CASE("3D outputs belong to render assets, 2D outputs to draw assets") {
    CHECK_THAT(diagnostic(with(thing("", "  output point tip"))), ContainsSubstring("point3d and box3d outputs"));
    CHECK_THAT(diagnostic({{"model.ips", "asset flat {\n  size 8x8\n  output point3d tip\n  draw { tip = {1, 2} }\n}\n"}}),
               ContainsSubstring("'point3d' outputs belong to a render asset"));
    CHECK_THAT(diagnostic(with(thing("    body = {{0, 0, 0}, 4.0}", "  output box3d body"))),
               ContainsSubstring("a box {at, size} needs two vec3 values"));
    CHECK_THAT(diagnostic(with(thing("    tip = {1, 2}", "  output point3d tip"))), ContainsSubstring("expects vec3"));
}

TEST_CASE("the outputs section is written only when a model declares outputs") {
    const auto plain = compile(with(thing("    box({0, 0, 0}, {1, 1, 1}, s)")));
    REQUIRE(plain.succeeded());
    CHECK(model::loadModel(plain.bytes).outputs.empty());
    const auto declared = compile(with(thing("    box({0, 0, 0}, {1, 1, 1}, s)", "  output point3d tip")));
    REQUIRE(declared.succeeded());
    const auto loaded = model::loadModel(declared.bytes);
    REQUIRE(loaded.outputs.size() == 1);
    CHECK(loaded.outputs[0].owner == model::kRootOwner);
    CHECK(declared.bytes.size() > plain.bytes.size());
}
