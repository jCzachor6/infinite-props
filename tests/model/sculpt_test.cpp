// The sculpt block (models v1.2, M6): the field of the lumps, the grid, the
// net and its skin straight from the kernel; then sculpt, carve and the
// lumps compiled from source, their diagnostics, their traps and their
// tariff.
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "iprop/model/Sculpt.hpp"
#include "ips/Parser.hpp"
#include "ips/Printer.hpp"
#include "model/support.hpp"

#include <cmath>
#include <map>
#include <set>
#include <utility>
#include <vector>

using namespace model_support;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;
using model::Lump;
using model::LumpKind;
using model::Point3;

namespace {

constexpr double kPi = 3.14159265358979323846;

Lump ball(Point3 at, double radius, bool carve = false) {
    Lump lump;
    lump.a = lump.b = at;
    lump.radii[0] = lump.radii[1] = lump.radii[2] = radius;
    lump.carve = carve;
    return lump;
}

Lump ball3(Point3 at, double rx, double ry, double rz) {
    Lump lump = ball(at, rx);
    lump.radii[1] = ry;
    lump.radii[2] = rz;
    return lump;
}

Lump capsule(Point3 a, Point3 b, double r0, double r1) {
    Lump lump;
    lump.kind = LumpKind::Capsule;
    lump.a = a;
    lump.b = b;
    lump.radii[0] = r0;
    lump.radii[1] = r1;
    return lump;
}

Lump slab(Point3 at, Point3 size, double round) {
    Lump lump;
    lump.kind = LumpKind::Slab;
    lump.a = lump.b = at;
    lump.radii[0] = size.x * 0.5;
    lump.radii[1] = size.y * 0.5;
    lump.radii[2] = size.z * 0.5;
    lump.round = round;
    return lump;
}

Point3 sub(const Point3& a, const Point3& b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
double dot(const Point3& a, const Point3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
Point3 cross(const Point3& a, const Point3& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
double length(const Point3& v) { return std::sqrt(dot(v, v)); }

struct Built {
    model::SculptGrid grid;
    model::SculptNet net;
    model::LocalMesh mesh;
};

// The kernel as the machine runs it. The count is taken first there, so it
// has to agree with what is built.
Built build(const std::vector<Lump>& lumps, double cell, double blend, model::Mapping mapping = {}) {
    Built built;
    built.grid = model::sculptGrid(lumps, cell, blend);
    const auto samples = model::sculptSamples(lumps, blend, cell, built.grid);
    built.net = model::sculptNet(built.grid, samples, cell);
    const auto counts = model::sculptCount(built.grid, samples);
    REQUIRE(counts.points == built.net.points.size());
    REQUIRE(counts.triangles == built.net.triangles.size());
    model::sculptMesh(built.mesh, built.net, lumps, blend, cell, mapping, 1);
    REQUIRE(built.mesh.vertices.size() == built.net.vertices);
    REQUIRE(built.mesh.triangles.size() == built.net.triangles.size());
    return built;
}

// Six times the volume inside the triangles, positive when they turn
// counterclockwise seen from outside.
double volume(const model::SculptNet& net) {
    double sum = 0.0;
    for (const auto& t : net.triangles) sum += dot(net.points[t[0]], cross(net.points[t[1]], net.points[t[2]]));
    return sum / 6.0;
}

// Every edge is walked as often one way as the other.
bool closed(const model::SculptNet& net) {
    std::map<std::pair<u32, u32>, int> edges;
    for (const auto& t : net.triangles)
        for (std::size_t c = 0; c < 3; ++c) ++edges[{t[c], t[(c + 1) % 3]}];
    for (const auto& [edge, count] : edges) {
        const auto back = edges.find({edge.second, edge.first});
        if (back == edges.end() || back->second != count) return false;
    }
    return !edges.empty();
}

double ballVolume(double radius) { return 4.0 / 3.0 * kPi * radius * radius * radius; }

// The same for a generated model: positive when every surface turns
// counterclockwise seen from outside.
double volume(const model::ModelResult& result) {
    double sum = 0.0;
    for (const auto& surface : result.surfaces) {
        const auto point = [&](u32 index) {
            return Point3{surface.positions[index * 3], surface.positions[index * 3 + 1], surface.positions[index * 3 + 2]};
        };
        for (std::size_t i = 0; i < surface.indices.size(); i += 3)
            sum += dot(point(surface.indices[i]), cross(point(surface.indices[i + 1]), point(surface.indices[i + 2])));
    }
    return sum / 6.0;
}

} // namespace

TEST_CASE("the field of a lump is its distance") {
    const auto field = [](const Lump& lump, Point3 p) { return model::sculptField(std::vector<Lump>{lump}, 0.0, p); };
    // A ball.
    CHECK_THAT(field(ball({1, 2, 3}, 4.0), {1, 2, 10}), WithinAbs(3.0, 1e-12));
    CHECK_THAT(field(ball({1, 2, 3}, 4.0), {1, 2, 3}), WithinAbs(-4.0, 1e-12));
    // A capsule of two radii is two balls and the cone that touches both:
    // beside the first end the ball, beside the middle the cone's side.
    const Lump cone = capsule({0, 0, 0}, {10, 0, 0}, 3.0, 1.0);
    CHECK_THAT(field(cone, {0, 5, 0}), WithinAbs(2.0, 1e-12));
    CHECK_THAT(field(cone, {5, 5, 0}), WithinAbs(1.0 + 5.0 * std::sqrt(0.96) - 3.0, 1e-12));
    CHECK_THAT(field(cone, {14, 0, 0}), WithinAbs(3.0, 1e-12));
    CHECK_THAT(field(cone, {-5, 0, 0}), WithinAbs(2.0, 1e-12));
    // Where the cone leaves a ball the value does not jump: the surface has no crease there.
    const double s = 0.2, c = std::sqrt(0.96);
    const Point3 leaving{3.0 * s, 3.0 * c, 0.0};
    CHECK_THAT(field(cone, leaving), WithinAbs(0.0, 1e-12));
    CHECK_THAT(field(cone, {leaving.x + 1e-7, leaving.y, 0}) - field(cone, {leaving.x - 1e-7, leaving.y, 0}),
               WithinAbs(2e-7 * s, 1e-12));
    // One end inside the other is the larger ball alone.
    CHECK_THAT(field(capsule({0, 0, 0}, {1, 0, 0}, 5.0, 1.0), {9, 0, 0}), WithinAbs(4.0, 1e-12));
    // A slab is a box with rounded edges.
    const Lump box = slab({0, 0, 0}, {4, 2, 6}, 0.5);
    CHECK_THAT(field(box, {5, 0, 0}), WithinAbs(3.0, 1e-12));
    CHECK_THAT(field(box, {0, 0, 0}), WithinAbs(-1.0, 1e-12));
    CHECK_THAT(field(box, {2, 1, 3}), WithinAbs(0.5 * std::sqrt(3.0) - 0.5, 1e-12)); // its corner, rounded away
    // A flat ball: exact along its axes, zero on its surface, and far off
    // its axes never below the distance to the sphere around it.
    const Lump flat = ball3({0, 0, 0}, 10.0, 1.0, 10.0);
    CHECK_THAT(field(flat, {0, 3, 0}), WithinAbs(2.0, 1e-12));
    CHECK_THAT(field(flat, {14, 0, 0}), WithinAbs(4.0, 1e-12));
    CHECK_THAT(field(flat, {10.0 * std::cos(0.7), std::sin(0.7), 0}), WithinAbs(0.0, 1e-12));
    CHECK(field(flat, {27.0, 2.78, 0}) >= std::sqrt(27.0 * 27.0 + 2.78 * 2.78) - 10.0);
    CHECK_THAT(field(flat, {0, 0, 0}), WithinAbs(-1.0, 1e-12));
}

TEST_CASE("lumps join by a smooth minimum and carve by the same maximum") {
    const std::vector<Lump> pair{ball({-3, 0, 0}, 2.0), ball({3, 0, 0}, 2.0)};
    // Half way both are one unit off: the join dips a quarter of the blend
    // less what their distance apart leaves of it.
    CHECK_THAT(model::sculptField(pair, 0.0, {0, 0, 0}), WithinAbs(1.0, 1e-12));
    CHECK_THAT(model::sculptField(pair, 2.0, {0, 0, 0}), WithinAbs(1.0 - 2.0 * 2.0 / (4.0 * 2.0), 1e-12));
    // Farther apart than the blend, the nearer lump alone.
    CHECK_THAT(model::sculptField(pair, 2.0, {-3, 0, 0}), WithinAbs(-2.0, 1e-12));
    // However many lumps are equally near, the join stays within one blend of them.
    std::vector<Lump> crowd;
    for (int i = 0; i < 64; ++i) crowd.push_back(ball({-3, 0, 0}, 2.0));
    const double dipped = model::sculptField(crowd, 2.0, {0, 0, 0});
    CHECK(dipped < 1.0 - 1.8);
    CHECK(dipped > 1.0 - 2.0);
    // A carved lump takes its inside away and leaves the rest.
    const std::vector<Lump> bitten{ball({0, 0, 0}, 4.0), ball({4, 0, 0}, 2.0, true)};
    CHECK_THAT(model::sculptField(bitten, 0.0, {3, 0, 0}), WithinAbs(1.0, 1e-12));
    CHECK_THAT(model::sculptField(bitten, 0.0, {-1, 0, 0}), WithinAbs(-3.0, 1e-12));
    // Lumps apply in order: what is carved first has nothing to cut.
    const std::vector<Lump> late{ball({4, 0, 0}, 2.0, true), ball({0, 0, 0}, 4.0)};
    CHECK_THAT(model::sculptField(late, 0.0, {3, 0, 0}), WithinAbs(-1.0, 1e-12));
    // Nothing at all is outside everywhere.
    CHECK(model::sculptField(std::vector<Lump>{}, 1.0, {0, 0, 0}) > 0.0);
}

TEST_CASE("a ball becomes a closed surface around its centre") {
    const Point3 centre{0.3, 0.1, -0.2};
    const double radius = 5.0, cell = 1.0;
    const Built built = build({ball(centre, radius)}, cell, 0.0);
    REQUIRE(built.net.points.size() > 100);
    for (const Point3& p : built.net.points) CHECK(std::fabs(length(sub(p, centre)) - radius) < 0.2 * cell);
    CHECK(closed(built.net));
    // A point is the mean of its cell's crossings, which lies a little
    // inside a round surface.
    CHECK_THAT(volume(built.net), WithinRel(ballVolume(radius), 0.06));
    CHECK(volume(built.net) < ballVolume(radius));
    for (const auto& vertex : built.mesh.vertices) {
        const Point3 out = sub(vertex.position, centre);
        CHECK_THAT(length(vertex.normal), WithinAbs(1.0, 1e-12));
        CHECK(dot(vertex.normal, out) / length(out) > 0.995);
        CHECK_THAT(length(vertex.tangent), WithinAbs(1.0, 1e-12));
        CHECK_THAT(dot(vertex.tangent, vertex.normal), WithinAbs(0.0, 1e-12));
        CHECK(vertex.w == 1.0);
    }
    // A finer cell follows the ball more closely.
    const Built fine = build({ball(centre, radius)}, 0.5, 0.0);
    CHECK(std::fabs(volume(fine.net) - ballVolume(radius)) < std::fabs(volume(built.net) - ballVolume(radius)));
    CHECK(fine.net.points.size() > 3 * built.net.points.size());
}

TEST_CASE("the grid of a sculpt lies on multiples of its cell") {
    const double cell = 0.5;
    const auto shape = [](Point3 by) {
        return std::vector<Lump>{ball({by.x, by.y, by.z}, 3.0),
                                 capsule({by.x + 2, by.y, by.z}, {by.x + 6, by.y + 3, by.z + 1}, 1.5, 0.75),
                                 slab({by.x - 3, by.y - 1, by.z + 2}, {3, 2, 4}, 0.5),
                                 ball({by.x, by.y + 3, by.z}, 1.5, true)};
    };
    const Built here = build(shape({0, 0, 0}), cell, 1.0);
    // Moved by whole cells, the same net, moved.
    const Point3 by{3 * cell, -2 * cell, 5 * cell};
    const Built there = build(shape(by), cell, 1.0);
    REQUIRE(there.net.points.size() == here.net.points.size());
    CHECK(there.net.triangles == here.net.triangles);
    double worst = 0.0;
    for (std::size_t i = 0; i < here.net.points.size(); ++i)
        worst = std::max(worst, length(sub(sub(there.net.points[i], by), here.net.points[i])));
    CHECK(worst < 1e-9);
    for (int axis = 0; axis < 3; ++axis) {
        const double step[3] = {3, -2, 5};
        CHECK(there.grid.origin[axis] == here.grid.origin[axis] + static_cast<i64>(step[axis]));
        CHECK(there.grid.nodes[axis] == here.grid.nodes[axis]);
    }
    // Moved by a part of a cell, the lumps cross other nodes: another net.
    const Point3 part{0.2, 0.0, 0.0};
    const Built between = build(shape(part), cell, 1.0);
    bool same = between.net.points.size() == here.net.points.size();
    for (std::size_t i = 0; same && i < here.net.points.size(); ++i)
        same = length(sub(sub(between.net.points[i], part), here.net.points[i])) < 1e-9;
    CHECK_FALSE(same);
    // Every shape of it is closed, and the nodes on the outside are outside.
    CHECK(closed(here.net));
    CHECK(closed(between.net));
}

TEST_CASE("joining adds to the surface and carving takes from it") {
    const double cell = 0.5;
    const Built one = build({ball({0, 0, 0}, 4.0)}, cell, 0.0);
    // Two balls that overlap, joined sharply and smoothly.
    const Built sharp = build({ball({0, 0, 0}, 4.0), ball({5, 0, 0}, 3.0)}, cell, 0.0);
    const Built smooth = build({ball({0, 0, 0}, 4.0), ball({5, 0, 0}, 3.0)}, cell, 2.0);
    CHECK(volume(sharp.net) > volume(one.net));
    CHECK(volume(sharp.net) < ballVolume(4.0) + ballVolume(3.0));
    CHECK(volume(smooth.net) > volume(sharp.net) * 1.01);
    CHECK(closed(smooth.net));
    // A carved ball takes a bite; with a blend the bite is wider.
    const Built bitten = build({ball({0, 0, 0}, 4.0), ball({4, 0, 0}, 2.0, true)}, cell, 0.0);
    const Built softer = build({ball({0, 0, 0}, 4.0), ball({4, 0, 0}, 2.0, true)}, cell, 1.0);
    CHECK(volume(bitten.net) < volume(one.net) * 0.97);
    CHECK(volume(softer.net) < volume(bitten.net));
    CHECK(closed(bitten.net));
    CHECK(volume(bitten.net) > 0.0);
    // A carve with nothing to cut, and no lump at all, build nothing.
    CHECK(model::sculptGrid(std::vector<Lump>{ball({0, 0, 0}, 4.0, true)}, cell, 1.0).samples() == 0);
    CHECK(model::sculptGrid(std::vector<Lump>{}, cell, 1.0).samples() == 0);
    // What is carved away whole leaves no surface.
    const Built gone = build({ball({0, 0, 0}, 2.0), ball({0, 0, 0}, 4.0, true)}, cell, 0.0);
    CHECK(gone.net.points.empty());
    CHECK(gone.net.triangles.empty());
    // Lumps that do not touch are two surfaces in one mesh.
    const Built apart = build({ball({0, 0, 0}, 2.0), ball({10, 0, 0}, 2.0)}, cell, 1.0);
    CHECK(closed(apart.net));
    CHECK_THAT(volume(apart.net), WithinRel(2.0 * ballVolume(2.0), 0.1));
    // A blend never leaves the grid: the join of many lumps is still closed.
    std::vector<Lump> heap;
    for (int i = 0; i < 12; ++i) heap.push_back(ball({std::cos(i * 0.5) * 2.0, std::sin(i * 0.5) * 2.0, i * 0.25}, 1.5));
    heap.push_back(ball3({0, 0, 1.5}, 6.0, 0.75, 6.0));
    CHECK(closed(build(heap, cell, 3.0).net));
}

TEST_CASE("a skin is laid on a sculpt by three projections") {
    model::Mapping mapping;
    mapping.density = 2.0;
    mapping.shiftU = 3.0;
    mapping.shiftV = 1.0;
    mapping.assetWidth = 16.0;
    mapping.assetHeight = 8.0;
    const Built built = build({ball({0.25, 0.5, -0.75}, 5.0)}, 1.0, 0.0, mapping);
    // The image's right and up for each side a triangle can face: those of
    // a box's faces.
    const Point3 right[6] = {{0, 0, -1}, {0, 0, 1}, {1, 0, 0}, {1, 0, 0}, {1, 0, 0}, {-1, 0, 0}};
    const Point3 up[6] = {{0, 1, 0}, {0, 1, 0}, {0, 0, -1}, {0, 0, 1}, {0, 1, 0}, {0, 1, 0}};
    std::set<int> seen;
    for (std::size_t t = 0; t < built.mesh.triangles.size(); ++t) {
        const auto& triangle = built.mesh.triangles[t];
        const model::LocalVertex* corner[3] = {&built.mesh.vertices[triangle.a], &built.mesh.vertices[triangle.b],
                                               &built.mesh.vertices[triangle.c]};
        // The side is the axis the face looks along most.
        const Point3 n = cross(sub(corner[1]->position, corner[0]->position), sub(corner[2]->position, corner[0]->position));
        const double size[3] = {std::fabs(n.x), std::fabs(n.y), std::fabs(n.z)};
        const double component[3] = {n.x, n.y, n.z};
        const int axis = size[0] >= size[1] && size[0] >= size[2] ? 0 : size[1] >= size[2] ? 1 : 2;
        const int side = axis * 2 + (component[axis] < 0.0 ? 1 : 0);
        REQUIRE(built.net.projection[t] == side);
        seen.insert(side);
        for (const auto* vertex : corner) {
            // One unit of the model is `density` texels of the image.
            const double u = (dot(vertex->position, right[side]) * 2.0 + 3.0) / 16.0;
            const double v = (-dot(vertex->position, up[side]) * 2.0 + 1.0) / 8.0;
            CHECK(vertex->uv[0] == static_cast<f32>(u));
            CHECK(vertex->uv[1] == static_cast<f32>(v));
            // The tangent runs where u rises, as far as the surface lets it.
            CHECK(dot(vertex->tangent, right[side]) > 0.5);
            CHECK(vertex->skin == 1);
        }
        CHECK(triangle.skin == 1);
        // The image is not mirrored: right, up and the face make a right hand.
        CHECK(dot(cross(right[side], up[side]), n) > 0.0);
    }
    CHECK(seen.size() == 6);
    // A point where two projections meet is a vertex in each.
    CHECK(built.mesh.vertices.size() > built.net.points.size());
    CHECK(built.mesh.vertices.size() < 2 * built.net.points.size());
}

TEST_CASE("a sculpt's grid has a limit") {
    // 128 cells on an axis hold a ball; more do not.
    // The grid reaches one blend and one cell beyond the lumps.
    CHECK(model::sculptGrid(std::vector<Lump>{ball({0, 0, 0}, 62.0)}, 1.0, 1.0).nodes[0] == 129);
    try {
        (void)model::sculptGrid(std::vector<Lump>{ball({0, 0, 0}, 62.5)}, 1.0, 1.0);
        FAIL("no violation");
    } catch (const model::Violation& violation) {
        CHECK_THAT(violation.what(), ContainsSubstring("sculpt grid exceeds 128 cells on an axis"));
    }
}

// ---- from source ------------------------------------------------------------

TEST_CASE("sculpt, carve and the lumps compile and build one surface") {
    const std::string body = R"(
    sculpt(1.0, 2.0, s) {
      ball({0, 8, 0}, 6.0)
      ball({7, 8, 0}, {4.0, 3.0, 5.0})
      capsule({0, 8, 0}, {-10, 14, 0}, 2.0)
      capsule({0, 8, 0}, {0, 8, 10}, 3.0, 1.0)
      slab({0, 1, 0}, {8, 2, 8}, 0.5)
      carve {
        ball({0, 15, 0}, 3.0)
      }
    })";
    const auto result = generate(with(thing(body)));
    REQUIRE(result.surfaces.size() == 1);
    REQUIRE(result.textures.size() == 1);
    CHECK(result.textures[0].repeats);
    CHECK(result.vertexCount > 500);
    CHECK(result.triangleCount > 1000);
    CHECK(result.surfaces[0].indices.size() == 3u * result.triangleCount);
    // The slab's foot stands on the ground and the capsule reaches left.
    CHECK_THAT(result.boundsMin[1], WithinAbs(0.0, 0.5));
    CHECK_THAT(result.boundsMin[0], WithinAbs(-12.0, 0.5));
    // A tip as small as the cell is rounded off a little.
    CHECK(result.boundsMax[2] > 10.f);
    CHECK(result.boundsMax[2] < 11.01f);
    // The bite: nothing is left near the centre of the carved ball.
    const auto& positions = result.surfaces[0].positions;
    for (std::size_t i = 0; i < positions.size(); i += 3) {
        const Point3 off{positions[i], positions[i + 1] - 15.0, positions[i + 2]};
        CHECK(length(off) > 2.0);
    }
    // The same source builds the same surface.
    const auto again = generate(with(thing(body)));
    CHECK(result.surfaces[0].positions == again.surfaces[0].positions);
    CHECK(result.surfaces[0].normals == again.surfaces[0].normals);
    CHECK(result.surfaces[0].uvs == again.surfaces[0].uvs);
    CHECK(result.surfaces[0].indices == again.surfaces[0].indices);
    // An empty sculpt and one with only a carve build nothing.
    CHECK(generate(with(thing("    sculpt(1.0, 2.0, s) { }"))).vertexCount == 0);
    CHECK(generate(with(thing("    sculpt(1.0, 2.0, s) { carve { ball({0, 0, 0}, 3.0) } }"))).vertexCount == 0);
}

TEST_CASE("a sculpt is the kernel's surface in the model") {
    // What the machine hands on is what the kernel builds from the same lumps.
    const auto result = generate(with(thing("    sculpt(0.5, 1.0, s) { ball({1, 2, 3}, 2.5)\n capsule({1, 2, 3}, {4, 2, 3}, 1.0, 0.5) }")));
    model::Mapping mapping;
    mapping.assetWidth = mapping.assetHeight = 8.0;
    const Built built = build({ball({1, 2, 3}, 2.5), capsule({1, 2, 3}, {4, 2, 3}, 1.0, 0.5)}, 0.5, 1.0, mapping);
    REQUIRE(result.vertexCount == built.mesh.vertices.size());
    REQUIRE(result.triangleCount == built.mesh.triangles.size());
    const auto& surface = result.surfaces[0];
    for (std::size_t i = 0; i < built.mesh.vertices.size(); ++i) {
        const auto& vertex = built.mesh.vertices[i];
        REQUIRE(bits(surface.positions[i * 3]) == bits(static_cast<f32>(vertex.position.x)));
        REQUIRE(bits(surface.positions[i * 3 + 1]) == bits(static_cast<f32>(vertex.position.y)));
        REQUIRE(bits(surface.positions[i * 3 + 2]) == bits(static_cast<f32>(vertex.position.z)));
        REQUIRE(bits(surface.uvs[i * 2]) == bits(vertex.uv[0]));
        REQUIRE(bits(surface.uvs[i * 2 + 1]) == bits(vertex.uv[1]));
        CHECK_THAT(surface.normals[i * 3], WithinAbs(vertex.normal.x, 1e-6));
        CHECK_THAT(surface.normals[i * 3 + 1], WithinAbs(vertex.normal.y, 1e-6));
        CHECK_THAT(surface.normals[i * 3 + 2], WithinAbs(vertex.normal.z, 1e-6));
        CHECK(surface.tangents[i * 4 + 3] == 1.f);
    }
    for (std::size_t t = 0; t < built.mesh.triangles.size(); ++t) {
        REQUIRE(surface.indices[t * 3] == built.mesh.triangles[t].a);
        REQUIRE(surface.indices[t * 3 + 1] == built.mesh.triangles[t].b);
        REQUIRE(surface.indices[t * 3 + 2] == built.mesh.triangles[t].c);
    }
}

TEST_CASE("place moves, turns, mirrors and scales lumps evenly") {
    const auto bounds = [](const std::string& body) { return generate(with(thing(body))); };
    const auto plain = bounds("    sculpt(0.5, 0.0, s) { ball({0, 0, 0}, 3.0) }");
    CHECK_THAT(plain.boundsMin[0], WithinAbs(-3.0, 0.1));
    CHECK_THAT(plain.boundsMax[0], WithinAbs(3.0, 0.1));
    // Inside the block a place carries the lump; the grid stays where it is.
    const auto moved = bounds("    sculpt(0.5, 0.0, s) { place(at = {10, 0, 0}) { ball({0, 0, 0}, 3.0) } }");
    CHECK_THAT(moved.boundsMin[0], WithinAbs(7.0, 0.1));
    CHECK(moved.vertexCount == plain.vertexCount);
    const auto doubled = bounds("    sculpt(0.5, 0.0, s) { place(scale = 2.0) { ball({1, 0, 0}, 1.5) } }");
    CHECK_THAT(doubled.boundsMin[0], WithinAbs(-1.0, 0.1));
    CHECK_THAT(doubled.boundsMax[0], WithinAbs(5.0, 0.1));
    const auto mirrored = bounds("    sculpt(0.5, 0.0, s) { place(mirror_x = 1) { capsule({2, 0, 0}, {8, 0, 0}, 1.0) } }");
    CHECK_THAT(mirrored.boundsMin[0], WithinAbs(-9.0, 0.25));
    CHECK_THAT(mirrored.boundsMax[0], WithinAbs(-1.0, 0.25));
    const auto turned = bounds("    sculpt(0.5, 0.0, s) { place(roll = 90.0) { ball({0, 0, 0}, {4.0, 1.0, 1.0}) } }");
    CHECK_THAT(turned.boundsMax[1], WithinAbs(4.0, 0.5));
    CHECK_THAT(turned.boundsMax[0], WithinAbs(1.0, 0.1));
    // A carve in a place, and a place in a carve, cut where they stand: the
    // rim of the bite is where the two balls cross, at x = 7/3.
    const auto bitten = bounds("    sculpt(0.5, 0.0, s) { ball({0, 0, 0}, 3.0)\n carve { place(at = {3, 0, 0}) { ball({0, 0, 0}, 2.0) } } }");
    CHECK(bitten.boundsMax[0] < 2.34f);
    CHECK(bitten.boundsMax[0] > 1.9f);
    const auto other = bounds("    sculpt(0.5, 0.0, s) { ball({0, 0, 0}, 3.0)\n place(at = {3, 0, 0}) { carve { ball({0, 0, 0}, 2.0) } } }");
    CHECK(other.surfaces[0].positions == bitten.surfaces[0].positions);
    // Around the block a place carries the finished surface, exactly.
    const auto outside = bounds("    place(at = {10, 0, 0}) { sculpt(0.5, 0.0, s) { ball({0, 0, 0}, 3.0) } }");
    REQUIRE(outside.vertexCount == plain.vertexCount);
    for (std::size_t i = 0; i < plain.surfaces[0].positions.size(); i += 3)
        REQUIRE_THAT(outside.surfaces[0].positions[i], WithinAbs(plain.surfaces[0].positions[i] + 10.0, 1e-5));
    // A lump stretched unequally is no distance any more.
    CHECK_THAT(trapMessage(with(thing("    sculpt(0.5, 0.0, s) { place(scale = {1, 2, 1}) { ball({0, 0, 0}, 3.0) } }"))),
               ContainsSubstring("a lump takes an equal scale on every axis"));
}

TEST_CASE("a sculpt is geometry like any other for the blocks around it") {
    const std::string lumps = "sculpt(1.0, 1.0, s) { ball({0, 6, 0}, 5.0)\n capsule({0, 6, 0}, {0, 16, 0}, 2.0) }";
    const auto plain = generate(with(thing("    " + lumps)));
    const auto smoothed = generate(with(thing("    smooth(60.0) { " + lumps + " }")));
    CHECK(smoothed.triangleCount == plain.triangleCount);
    const auto rough = generate(with(thing("    displace(0.5, 0.3, 7) { " + lumps + " }")));
    CHECK(rough.triangleCount == plain.triangleCount);
    CHECK(rough.surfaces[0].positions != plain.surfaces[0].positions);
    const auto fine = generate(with(thing("    refine(2) { " + lumps + " }")));
    CHECK(fine.triangleCount == plain.triangleCount * 4);
    const auto bent = generate(with(thing("    bend(60.0) { " + lumps + " }")));
    CHECK(bent.boundsMax[0] > plain.boundsMax[0] + 1.f);
    // A primitive beside it shares the model, and a second sculpt its own grid.
    const auto both = generate(with(thing("    box({20, 0, 0}, {4, 4, 4}, s)\n    " + lumps + "\n    place(at = {-20, 0, 0}) { " + lumps + " }")));
    CHECK(both.triangleCount == 12 + 2 * plain.triangleCount);
    // A point written in a sculpt is where it was written.
    const auto marked = generate(with(thing("    place(at = {1, 2, 3}) { sculpt(1.0, 1.0, s) { ball({0, 6, 0}, 5.0)\n carve { top = {0, 11, 0} } } }",
                                           "  output point3d top")));
    const auto& top = output(marked, "top");
    REQUIRE(top.points.size() == 1);
    CHECK(top.points[0].x == 1.f);
    CHECK(top.points[0].y == 13.f);
    CHECK(top.points[0].z == 3.f);
    // The level of detail can choose the cell.
    const auto loaded = load(with(thing("    cell = 0.5 * float(1 << model.lod())\n    sculpt(cell, 1.0, s) { ball({0, 6, 0}, 5.0) }")));
    model::ModelInputs nearby, distant;
    distant.lod = 2;
    CHECK(model::generateModel(loaded, distant).triangleCount * 8 < model::generateModel(loaded, nearby).triangleCount);
}

TEST_CASE("a sculpt keeps its outside out under a mirror, in a part and when it is left early") {
    const std::string lumps = "sculpt(0.5, 1.0, s) { ball({3, 4, 0}, 3.0)\n capsule({3, 4, 0}, {8, 6, 1}, 1.5, 1.0) }";
    const auto plain = generate(with(thing("    " + lumps)));
    CHECK_THAT(volume(plain), WithinRel(ballVolume(3.0) + 30.0, 0.25));
    // A mirror around the block turns the surface over and its triangles back.
    const auto mirrored = generate(with(thing("    place(mirror_x = 1) { " + lumps + " }")));
    CHECK_THAT(volume(mirrored), WithinRel(volume(plain), 1e-5));
    CHECK_THAT(mirrored.boundsMin[0], WithinAbs(-plain.boundsMax[0], 1e-4));
    for (std::size_t i = 3; i < mirrored.surfaces[0].tangents.size(); i += 4) REQUIRE(mirrored.surfaces[0].tangents[i] == -1.f);
    // A mirror inside the block moves the lumps; the surface is built as ever.
    const auto within = generate(with(thing("    sculpt(0.5, 1.0, s) { place(mirror_x = 1) { ball({3, 4, 0}, 3.0)\n capsule({3, 4, 0}, {8, 6, 1}, 1.5, 1.0) } }")));
    CHECK_THAT(volume(within), WithinRel(volume(plain), 0.02));
    for (std::size_t i = 3; i < within.surfaces[0].tangents.size(); i += 4) REQUIRE(within.surfaces[0].tangents[i] == 1.f);

    // A part may sculpt; its surface comes through the call like any other.
    const char* const pebble = R"(
import flat from "plain.ips"
asset pebble {
  size 10x10x10
  input f32 wide = 3.0
  render {
    sculpt(0.5, 1.0, flat.draw(1)) { ball({0, 2, 0}, {wide, 2.0, 2.5}) }
  }
}
)";
    const Sources sources{{"model.ips", "import pebble from \"pebble.ips\"\nasset heap {\n  size 40x40x40\n  render {\n"
                                        "    place(at = {10, 0, 0}) { pebble.render(1, wide = 4.0) }\n    pebble.render(2)\n  }\n}\n"},
                          {"plain.ips", kPlain}, {"pebble.ips", pebble}};
    const auto heap = generate(sources);
    CHECK(heap.triangleCount > 200);
    CHECK_THAT(heap.boundsMax[0], WithinAbs(14.0, 0.3));
    CHECK(volume(heap) > 0.0);

    // Leaving a block early builds what it holds so far: a break in the
    // block, a return in a carve.
    const auto broken = generate(with(thing(R"(
    for i in 0..3 {
      sculpt(0.5, 0.0, s) {
        ball({float(i) * 10.0, 0, 0}, 2.0)
        if (i == 1) { break }
        ball({float(i) * 10.0, 6, 0}, 2.0)
      }
    })")));
    const auto three = generate(with(thing("    sculpt(0.5, 0.0, s) { ball({0, 0, 0}, 2.0)\n ball({0, 6, 0}, 2.0)\n ball({10, 0, 0}, 2.0) }")));
    CHECK(broken.triangleCount == three.triangleCount);
    const auto returned = generate(with(thing("    sculpt(0.5, 0.0, s) { ball({0, 0, 0}, 3.0)\n bite() }\n    box({9, 0, 0}, {1, 1, 1}, s)", "",
                                              "  bite() {\n    carve {\n      ball({3, 0, 0}, 2.0)\n      return\n    }\n  }")));
    const auto bitten = generate(with(thing("    sculpt(0.5, 0.0, s) { ball({0, 0, 0}, 3.0)\n carve { ball({3, 0, 0}, 2.0) } }\n    box({9, 0, 0}, {1, 1, 1}, s)")));
    CHECK(returned.surfaces[0].positions == bitten.surfaces[0].positions);
    CHECK(returned.triangleCount == bitten.triangleCount);
}

TEST_CASE("the formatter writes sculpt and carve as they are read") {
    const std::string source = thing("    sculpt(1.0, 2.0, s) {\n      ball({0, 4, 0}, 3.0)\n      carve {\n"
                                     "        slab({0, 6, 0}, {2, 2, 2}, 0.5)\n      }\n    }");
    const auto formatted = ips::formatProgram(ips::parse(source));
    CHECK_THAT(formatted, ContainsSubstring("carve {"));
    CHECK_THAT(formatted, !ContainsSubstring("carve("));
    CHECK_THAT(formatted, ContainsSubstring("sculpt(1.0, 2.0, s) {"));
    CHECK(ips::formatProgram(ips::parse(formatted)) == formatted);
}

TEST_CASE("the checker knows sculpt, carve and the lumps") {
    const auto says = [](const std::string& body, const std::string& helpers = "") {
        return diagnostic(with(thing(body, "", helpers)));
    };
    CHECK_THAT(diagnostic({{"model.ips", "asset flat {\n  size 8x8\n  draw { sculpt(1.0, 2.0, 3) { } }\n}\n"}}),
               ContainsSubstring("sculpt shapes 3D geometry; it is available in a render asset"));
    CHECK_THAT(diagnostic({{"model.ips", "asset flat {\n  size 8x8\n  draw { carve { } }\n}\n"}}),
               ContainsSubstring("carve shapes 3D geometry; it is available in a render asset"));
    CHECK_THAT(says("    sculpt(1.0, 2.0) { }"), ContainsSubstring("sculpt takes 3 arguments (cell, blend, skin)"));
    CHECK_THAT(says("    sculpt { }"), ContainsSubstring("sculpt takes 3 arguments (cell, blend, skin)"));
    CHECK_THAT(says("    sculpt(1.0, 2.0, 3) { }"), ContainsSubstring("'skin' of sculpt must be a skin, got i32"));
    CHECK_THAT(says("    sculpt(s, 2.0, s) { }"), ContainsSubstring("'cell' of sculpt must be a number"));
    CHECK_THAT(says("    sculpt(cell = 1.0, blend = 2.0, skin = s) { }"), ContainsSubstring("takes its arguments in order"));
    // carve has one spelling.
    CHECK_THAT(says("    sculpt(1.0, 2.0, s) { carve() { } }"), ContainsSubstring("carve takes no arguments; write carve { ... }"));
    CHECK_THAT(says("    sculpt(1.0, 2.0, s) { carve(1.0) { } }"), ContainsSubstring("carve takes no arguments"));
    // What a sculpt block takes.
    CHECK_THAT(says("    sculpt(1.0, 2.0, s) { box({0, 0, 0}, {1, 1, 1}, s) }"),
               ContainsSubstring("box cannot be written in a sculpt block, which takes lumps: ball, capsule and slab"));
    CHECK_THAT(says("    sculpt(1.0, 2.0, s) { carve { sphere({0, 0, 0}, 2.0, 8, 4, s) } }"),
               ContainsSubstring("sphere cannot be written in a sculpt block"));
    CHECK_THAT(says("    sculpt(1.0, 2.0, s) { place(at = {1, 0, 0}) { tube({0, 0, 0}, {0, 1, 0}, {0, 2, 0}, {0, 3, 0}, 1.0, 1.0, 6, 4, s) } }"),
               ContainsSubstring("tube cannot be written in a sculpt block"));
    CHECK_THAT(says("    sculpt(1.0, 2.0, s) { sculpt(1.0, 2.0, s) { } }"),
               ContainsSubstring("a sculpt block cannot hold another"));
    CHECK_THAT(says("    sculpt(1.0, 2.0, s) { smooth(30.0) { } }"),
               ContainsSubstring("smooth cannot be written in a sculpt block, which takes lumps, carve and place; "
                                 "write the sculpt block in smooth instead"));
    CHECK_THAT(says("    sculpt(1.0, 2.0, s) { carve { carve { } } }"), ContainsSubstring("carve cannot be written in another carve"));
    // Where lumps and carve belong.
    CHECK_THAT(says("    ball({0, 0, 0}, 2.0)"), ContainsSubstring("ball is a lump and belongs in a sculpt block"));
    CHECK_THAT(says("    smooth(30.0) { capsule({0, 0, 0}, {1, 0, 0}, 2.0) }"),
               ContainsSubstring("capsule is a lump and belongs in a sculpt block"));
    CHECK_THAT(says("    carve { }"), ContainsSubstring("carve belongs in a sculpt block"));
    // The lumps check their arguments.
    CHECK_THAT(says("    sculpt(1.0, 2.0, s) { ball({0, 0, 0}, 2.0, s) }"), ContainsSubstring("ball"));
    CHECK_THAT(says("    sculpt(1.0, 2.0, s) { slab({0, 0, 0}, {1, 1, 1}) }"), ContainsSubstring("slab"));
    CHECK_THAT(says("    sculpt(1.0, 2.0, s) { capsule({0, 0, 0}, 2.0) }"), ContainsSubstring("capsule"));
    // A helper may hold lumps or a carve: where it is called decides.
    CHECK(compile(with(thing("    sculpt(1.0, 2.0, s) { knob(2.0)\n dent() }", "",
                             "  knob(r: f32) { ball({0, 0, 0}, r) }\n  dent() { carve { ball({0, 2, 0}, 1.0) } }")))
              .succeeded());
    // In a render asset the lumps are builtins; a draw asset keeps the names.
    CHECK_THAT(says("    x = 1", "  ball(r: f32) { }"), ContainsSubstring("ball"));
    CHECK(compile({{"model.ips", "asset flat {\n  size 8x8\n  draw { ball(2)\n carve(1)\n slab(1)\n capsule(1)\n sculpt(1) }\n"
                                 "  ball(n: i32) { rect(0, 0, n, n, #fff) }\n  carve(n: i32) { }\n  slab(n: i32) { }\n"
                                 "  capsule(n: i32) { }\n  sculpt(n: i32) { }\n}\n"}})
              .succeeded());
}

TEST_CASE("a part cannot be built among lumps") {
    const char* const peg = R"(
import flat from "plain.ips"
asset peg {
  size 4x10x4
  render {
    box({0, 0, 0}, {1, 4, 1}, flat.draw(1))
  }
}
)";
    const auto board = [&](const std::string& body, const std::string& helpers = "") {
        return Sources{{"model.ips", "import peg from \"peg.ips\"\nimport flat from \"plain.ips\"\nasset board {\n  size 40x40x40\n"
                                     "  render {\n    s = flat.draw(1)\n" + body + "\n  }\n" + helpers + "\n}\n"},
                       {"plain.ips", kPlain}, {"peg.ips", peg}};
    };
    CHECK_THAT(diagnostic(board("    sculpt(1.0, 2.0, s) { peg.render(1) }")),
               ContainsSubstring("a part cannot be built in a sculpt block"));
    CHECK_THAT(trapMessage(board("    sculpt(1.0, 2.0, s) { stand() }", "  stand() { peg.render(1) }")),
               ContainsSubstring("a sculpt block takes lumps: ball, capsule and slab"));
    // Beside the block a part is welcome.
    CHECK(generate(board("    sculpt(1.0, 2.0, s) { ball({0, 5, 0}, 3.0) }\n    peg.render(1)")).triangleCount > 12);
}

TEST_CASE("sculpt, carve and the lumps check what they are given when they run") {
    const auto trap = [](const std::string& body, const std::string& helpers = "") {
        return trapMessage(with(thing(body, "", helpers)));
    };
    CHECK_THAT(trap("    c = 0.0\n    sculpt(c, 1.0, s) { }"), ContainsSubstring("sculpt cell must be finite and positive"));
    CHECK_THAT(trap("    b = -1.0\n    sculpt(1.0, b, s) { }"), ContainsSubstring("sculpt blend must be finite and not negative"));
    CHECK_THAT(trap("    sculpt(1.0, 1.0, skin.fit(s)) { }"), ContainsSubstring("sculpt takes a skin that repeats"));
    CHECK_THAT(trap("    sculpt(1.0, 1.0, skin.region(s, 0.0, 0.0, 4.0, 4.0)) { }"),
               ContainsSubstring("sculpt takes a skin that repeats"));
    CHECK_THAT(trap("    r = 0.0\n    sculpt(1.0, 1.0, s) { ball({0, 0, 0}, r) }"),
               ContainsSubstring("ball needs a finite centre and a positive radius"));
    CHECK_THAT(trap("    r = -1.0\n    sculpt(1.0, 1.0, s) { ball({0, 0, 0}, {1.0, r, 1.0}) }"),
               ContainsSubstring("ball needs a finite centre and positive radii"));
    CHECK_THAT(trap("    r = 0.0\n    sculpt(1.0, 1.0, s) { capsule({0, 0, 0}, {1, 0, 0}, 1.0, r) }"),
               ContainsSubstring("capsule needs finite ends and positive radii"));
    CHECK_THAT(trap("    r = 0.75\n    sculpt(1.0, 1.0, s) { slab({0, 0, 0}, {4, 1, 4}, r) }"),
               ContainsSubstring("slab round must be from 0 to half its smallest side"));
    CHECK_THAT(trap("    w = 0.0\n    sculpt(1.0, 1.0, s) { slab({0, 0, 0}, {w, 1, 4}, 0.0) }"),
               ContainsSubstring("slab needs a finite centre and a positive size"));
    // A slab may be rounded to half its smallest side, and not at all.
    CHECK(trap("    sculpt(0.5, 1.0, s) { slab({0, 0, 0}, {4, 1, 4}, 0.5)\n slab({0, 3, 0}, {4, 1, 4}, 0.0) }") == "no trap");
    // The limits of one block.
    CHECK_THAT(trap("    sculpt(0.25, 1.0, s) { ball({0, 0, 0}, 20.0) }"),
               ContainsSubstring("sculpt grid exceeds 128 cells on an axis"));
    CHECK_THAT(trap("    sculpt(1.0, 1.0, s) { for i in 0..65 { ball({0, 0, 0}, 2.0) } }"),
               ContainsSubstring("sculpt exceeds 64 lumps"));
    CHECK(trap("    sculpt(1.0, 1.0, s) { for i in 0..64 { ball({0, 0, 0}, 2.0) } }") == "no trap");
    // Through a helper nothing is visible to the checker; the machine traps.
    CHECK_THAT(trap("    sculpt(1.0, 1.0, s) { crate(s) }", "  crate(k: skin) { box({0, 0, 0}, {1, 1, 1}, k) }"),
               ContainsSubstring("a sculpt block takes lumps: ball, capsule and slab"));
    CHECK_THAT(trap("    sculpt(1.0, 1.0, s) { wrap() }", "  wrap() { smooth(30.0) { } }"),
               ContainsSubstring("a sculpt block takes lumps: ball, capsule and slab"));
    CHECK_THAT(trap("    sculpt(1.0, 1.0, s) { again(s) }", "  again(k: skin) { sculpt(1.0, 1.0, k) { } }"),
               ContainsSubstring("a sculpt block takes lumps: ball, capsule and slab"));
    CHECK_THAT(trap("    knob()", "  knob() { ball({0, 0, 0}, 2.0) }"), ContainsSubstring("ball belongs in a sculpt block"));
    CHECK_THAT(trap("    smooth(30.0) { knob() }", "  knob() { slab({0, 0, 0}, {1, 1, 1}, 0.0) }"),
               ContainsSubstring("slab belongs in a sculpt block"));
    CHECK_THAT(trap("    dent()", "  dent() { carve { } }"), ContainsSubstring("carve belongs in a sculpt block"));
    CHECK_THAT(trap("    sculpt(1.0, 1.0, s) { carve { dent() } }", "  dent() { carve { } }"),
               ContainsSubstring("carve cannot be nested"));
    // A model has its limit of vertices whatever builds them.
    CHECK_THAT(trap("    sculpt(0.5, 0.0, s) { ball({0, 0, 0}, 31.0) }"), ContainsSubstring("model exceeds 65536 vertices"));
}

TEST_CASE("the M6 tariff: a sculpt pays for its samples and its surface") {
    CHECK(model::kSculptSampleFuel == 1);
    CHECK(model::kSculptNormalSamples == 6);
    CHECK(model::kMaxLumps == 64);
    CHECK(model::kMaxSculptCells == 128);

    const auto run = [](const std::string& body) { return generate(with(thing(body))); };
    const u64 none = run("").fuelUsed;
    // An empty block: its two numbers and its skin, its opening with the
    // reserve, and its end. It samples nothing.
    CHECK(run("    sculpt(1.0, 0.0, s) { }").fuelUsed - none == 3 + (1 + model::kBlockFuel) + 1);

    // What a block costs beyond its instructions, from the kernel's counts.
    const auto kernel = [](const std::vector<Lump>& lumps, double cell, double blend) {
        const Built built = build(lumps, cell, blend);
        const u64 count = lumps.size();
        return built.grid.samples() * count * model::kSculptSampleFuel +
               built.net.vertices * (model::kVertexFuel + model::kPassVertexFuel) +
               built.net.triangles.size() * (model::kTriangleFuel + model::kPassTriangleFuel) +
               built.net.points.size() * model::kSculptNormalSamples * count * model::kSculptSampleFuel;
    };
    // Two programs of the same instructions and another radius differ by
    // what the kernel does.
    const u64 lesser = run("    sculpt(1.0, 0.0, s) { ball({0, 0, 0}, 3.0) }").fuelUsed;
    const u64 greater = run("    sculpt(1.0, 0.0, s) { ball({0, 0, 0}, 6.0) }").fuelUsed;
    CHECK(greater - lesser == kernel({ball({0, 0, 0}, 6.0)}, 1.0, 0.0) - kernel({ball({0, 0, 0}, 3.0)}, 1.0, 0.0));
    // A second lump is sampled at every node and for every normal.
    const std::vector<Lump> twice{ball({0, 0, 0}, 3.0), ball({0, 0, 0}, 3.0)};
    const std::vector<Lump> beside{ball({0, 0, 0}, 3.0), ball({6, 2, 1}, 2.0)};
    const u64 doubled = run("    sculpt(1.0, 0.0, s) { ball({0, 0, 0}, 3.0)\n ball({0, 0, 0}, 3.0) }").fuelUsed;
    const u64 paired = run("    sculpt(1.0, 0.0, s) { ball({0, 0, 0}, 3.0)\n ball({6, 2, 1}, 2.0) }").fuelUsed;
    CHECK(paired - doubled == kernel(beside, 1.0, 0.0) - kernel(twice, 1.0, 0.0));
    // A carve is a scope: its opening and its end, and its lump like any other.
    const u64 carved = run("    sculpt(1.0, 0.0, s) { ball({0, 0, 0}, 3.0)\n carve { ball({6, 2, 1}, 2.0) } }").fuelUsed;
    const std::vector<Lump> cut{ball({0, 0, 0}, 3.0), ball({6, 2, 1}, 2.0, true)};
    CHECK(carved - paired == 2 + kernel(cut, 1.0, 0.0) - kernel(beside, 1.0, 0.0));
    // The whole price of a block: its opening, the instructions of its
    // lumps and the kernel, and nothing else.
    const u64 opening = 3 + (1 + model::kBlockFuel) + 1;
    const u64 call = doubled - lesser - (kernel(twice, 1.0, 0.0) - kernel({ball({0, 0, 0}, 3.0)}, 1.0, 0.0));
    CHECK(lesser - none == opening + call + kernel({ball({0, 0, 0}, 3.0)}, 1.0, 0.0));
    CHECK(call < 16);
    // Work counts the same samples.
    const auto first = run("    sculpt(1.0, 0.0, s) { ball({0, 0, 0}, 3.0) }");
    const auto second = run("    sculpt(1.0, 0.0, s) { ball({0, 0, 0}, 6.0) }");
    CHECK(second.workUsed > first.workUsed);
}
