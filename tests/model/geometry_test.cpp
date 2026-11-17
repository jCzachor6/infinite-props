// The geometry kernels of the model VM (spec models v1.2): every primitive
// is checked for the properties any correct mesh has, under the identity and
// under placements that turn, scale and mirror it.
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "iprop/model/Geometry.hpp"
#include "iprop/model/Ops.hpp"

#include <array>
#include <cmath>
#include <cstring>
#include <map>
#include <string>
#include <vector>

using namespace iprop;
using namespace iprop::model;

namespace {

struct V3 {
    double x, y, z;
};
V3 operator-(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
V3 cross(V3 a, V3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
double dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
double norm(V3 a) { return std::sqrt(dot(a, a)); }
V3 position(const Vertex& v) { return {v.position[0], v.position[1], v.position[2]}; }
V3 normal(const Vertex& v) { return {v.normal[0], v.normal[1], v.normal[2]}; }
V3 tangent(const Vertex& v) { return {v.tangent[0], v.tangent[1], v.tangent[2]}; }

// A mapping that keeps face coordinates readable: one unit is one texel of
// a 64 x 64 asset.
Mapping repeat() { return {false, 1.0, 0.0, 0.0, 64.0, 64.0}; }

template<class Shape> Mesh build(const Shape& shape, const Affine& scope = {}, Mapping mapping = repeat()) {
    Mesh mesh;
    const Counts counts = count(shape);
    emit(Target{mesh, scope, mapping, 1}, shape);
    REQUIRE(mesh.vertices.size() == counts.vertices);
    REQUIRE(mesh.triangles.size() == counts.triangles);
    REQUIRE(mesh.vertexSkins.size() == mesh.vertices.size());
    return mesh;
}

// Six times the signed volume enclosed by the triangles.
double volume(const Mesh& mesh) {
    double sum = 0.0;
    for (const Triangle& t : mesh.triangles)
        sum += dot(position(mesh.vertices[t.a]), cross(position(mesh.vertices[t.b]), position(mesh.vertices[t.c])));
    return sum / 6.0;
}

// What every primitive must satisfy: unit normals and tangents at right
// angles, triangles of positive area whose winding agrees with their vertex
// normals, and tangents and bitangent signs that agree with the UVs.
void checkSurface(const Mesh& mesh) {
    for (const Vertex& v : mesh.vertices) {
        REQUIRE_THAT(norm(normal(v)), Catch::Matchers::WithinAbs(1.0, 1e-6));
        REQUIRE_THAT(norm(tangent(v)), Catch::Matchers::WithinAbs(1.0, 1e-6));
        REQUIRE(std::fabs(dot(normal(v), tangent(v))) < 1e-5);
        REQUIRE(std::fabs(v.tangent[3]) == 1.f);
    }
    for (const Triangle& t : mesh.triangles) {
        INFO("triangle " << (&t - mesh.triangles.data()) << " of " << mesh.triangles.size() << ": " << t.a << " "
                         << t.b << " " << t.c);
        const Vertex* corner[3] = {&mesh.vertices[t.a], &mesh.vertices[t.b], &mesh.vertices[t.c]};
        const V3 e1 = position(*corner[1]) - position(*corner[0]), e2 = position(*corner[2]) - position(*corner[0]);
        const V3 face = cross(e1, e2);
        REQUIRE(norm(face) > 1e-9);
        for (const Vertex* v : corner) REQUIRE(dot(face, normal(*v)) > 0.0);

        const double du1 = corner[1]->uv[0] - corner[0]->uv[0], dv1 = corner[1]->uv[1] - corner[0]->uv[1];
        const double du2 = corner[2]->uv[0] - corner[0]->uv[0], dv2 = corner[2]->uv[1] - corner[0]->uv[1];
        const double det = du1 * dv2 - du2 * dv1;
        if (std::fabs(det) < 1e-12) continue;
        // The directions of growing u and of growing v on the surface.
        const V3 alongU{(e1.x * dv2 - e2.x * dv1) / det, (e1.y * dv2 - e2.y * dv1) / det, (e1.z * dv2 - e2.z * dv1) / det};
        const V3 alongV{(e2.x * du1 - e1.x * du2) / det, (e2.y * du1 - e1.y * du2) / det, (e2.z * du1 - e1.z * du2) / det};
        // The frames of the three corners, taken together: on a coarse
        // round surface one corner alone may lean far from the face.
        V3 along{0, 0, 0}, up{0, 0, 0};
        for (const Vertex* v : corner) {
            const V3 t3 = tangent(*v), b3 = cross(normal(*v), t3);
            along = {along.x + t3.x, along.y + t3.y, along.z + t3.z};
            // w * (N x T) points up the image, against growing v.
            up = {up.x + b3.x * v->tangent[3], up.y + b3.y * v->tangent[3], up.z + b3.z * v->tangent[3]};
        }
        REQUIRE(dot(alongU, along) > 0.0);
        REQUIRE(dot(up, alongV) < 0.0);
    }
}

// A closed solid: every edge, by position, is used once in each direction.
void checkClosed(const Mesh& mesh) {
    using Key = std::array<u32, 6>;
    std::map<Key, int> edges;
    const auto bits = [](f32 value) {
        u32 out;
        if (value == 0.f) value = 0.f; // one zero
        std::memcpy(&out, &value, 4);
        return out;
    };
    const auto key = [&](const Vertex& p, const Vertex& q) {
        return Key{bits(p.position[0]), bits(p.position[1]), bits(p.position[2]),
                   bits(q.position[0]), bits(q.position[1]), bits(q.position[2])};
    };
    for (const Triangle& t : mesh.triangles) {
        const u32 index[3] = {t.a, t.b, t.c};
        for (int i = 0; i < 3; ++i) ++edges[key(mesh.vertices[index[i]], mesh.vertices[index[(i + 1) % 3]])];
    }
    for (const auto& [edge, uses] : edges) {
        REQUIRE(uses == 1);
        const Key reverse{edge[3], edge[4], edge[5], edge[0], edge[1], edge[2]};
        REQUIRE(edges.count(reverse) == 1);
    }
    REQUIRE(volume(mesh) > 0.0);
}

Affine placed(u16 mask, Vec3 at, f32 yaw, f32 pitch, f32 roll, f32 scale, int mirrorX) {
    Placement p;
    p.mask = mask;
    p.at = at;
    p.yaw = yaw;
    p.pitch = pitch;
    p.roll = roll;
    p.scale[0] = p.scale[1] = p.scale[2] = scale;
    p.mirror[0] = mirrorX;
    return place(Affine{}, p);
}

// The scopes every primitive is emitted under.
std::vector<Affine> scopes() {
    return {Affine{},
            placed(kPlaceAt | kPlaceYaw, {3, 1, -2}, 90.f, 0, 0, 1, 0),
            placed(kPlaceAt | kPlaceYaw | kPlacePitch | kPlaceRoll | kPlaceScale, {-4, 2, 5}, 33.f, -17.f, 8.f, 1.5f, 0),
            placed(kPlaceMirrorX, {}, 0, 0, 0, 1, 1),
            placed(kPlaceYaw | kPlacePitch | kPlaceScale | kPlaceMirrorX, {}, 120.f, 25.f, 0, 0.5f, 1)};
}

template<class Shape> void checkEverywhere(const Shape& shape, bool closed) {
    int index = 0;
    for (const Affine& scope : scopes()) {
        INFO("scope " << index++);
        const Mesh mesh = build(shape, scope);
        checkSurface(mesh);
        if (closed) checkClosed(mesh);
    }
    // The fit mapping keeps every invariant too.
    Mapping fit = repeat();
    fit.fit = true;
    checkSurface(build(shape, Affine{}, fit));
}

std::vector<Vec2> points(std::initializer_list<Vec2> list) { return list; }

} // namespace

TEST_CASE("the pinned sine of the model kernels") {
    CHECK(sin64(0.0) == 0.0);
    CHECK_THAT(sin64(0.5), Catch::Matchers::WithinAbs(std::sin(0.5), 1e-11));
    CHECK_THAT(sin64(2.0), Catch::Matchers::WithinAbs(std::sin(2.0), 1e-11));
    CHECK_THAT(sin64(-4.5), Catch::Matchers::WithinAbs(std::sin(-4.5), 1e-11));
}

TEST_CASE("a box") {
    const BoxShape box = checkBox({1, 2, 3}, {4, 6, 2});
    const Mesh mesh = build(box);
    CHECK(mesh.vertices.size() == 24);
    CHECK(mesh.triangles.size() == 12);
    CHECK_THAT(volume(mesh), Catch::Matchers::WithinAbs(48.0, 1e-9));
    // The front face: its lower left corner, facing +Z, with the image's
    // lower left corner (u 0, v at the face height).
    const Vertex& first = mesh.vertices[0];
    CHECK(first.position[0] == -1.f);
    CHECK(first.position[1] == 2.f);
    CHECK(first.position[2] == 4.f);
    CHECK(first.normal[2] == 1.f);
    CHECK(first.tangent[0] == 1.f);
    CHECK(first.tangent[3] == 1.f);
    CHECK(first.uv[0] == 0.f);
    CHECK(first.uv[1] == 6.f / 64.f);
    checkEverywhere(box, true);
}

TEST_CASE("a bevelled box") {
    const BoxShape box = checkBox({0, 0, 0}, {4, 6, 2}, 0.5f);
    const Mesh mesh = build(box);
    CHECK(mesh.vertices.size() == 96);
    CHECK(mesh.triangles.size() == 44);
    // The bevel removes twelve edge prisms and adds back what the corners keep.
    // Twelve edge prisms of b*b/2 are cut away, and each of the eight
    // corners keeps two thirds of b cubed of what they share.
    CHECK_THAT(volume(mesh), Catch::Matchers::WithinAbs(48.0 - 6.0 + 2.0 / 3.0, 1e-9));
    checkEverywhere(box, true);
    checkEverywhere(checkBox({0, 0, 0}, {1, 1, 1}, 0.25f), true);
}

TEST_CASE("box arguments are checked") {
    CHECK_THROWS_AS(checkBox({0, 0, 0}, {0, 1, 1}), Violation);
    CHECK_THROWS_AS(checkBox({0, 0, 0}, {1, -1, 1}), Violation);
    CHECK_THROWS_AS(checkBox({0, 0, 0}, {2, 2, 2}, 1.f), Violation);
    CHECK_THROWS_AS(checkBox({0, 0, 0}, {2, 2, 2}, 0.f), Violation);
    CHECK_THROWS_AS(checkBox({NAN, 0, 0}, {2, 2, 2}), Violation);
}

TEST_CASE("cones, cylinders and bands") {
    for (const int sides : {3, 4, 5, 8, 12, 64}) {
        checkEverywhere(checkCone({0, 0, 0}, 2, 2, 5, sides, true), true);
        checkEverywhere(checkCone({1, 0, 1}, 3, 1, 4, sides, true), true);
        checkEverywhere(checkCone({0, 0, 0}, 2, 0, 5, sides, true), true);
        checkEverywhere(checkCone({0, 0, 0}, 0, 2, 5, sides, true), true);
        checkEverywhere(checkCone({0, 0, 0}, 2, 3, 1, sides, false), false);
    }
    const Mesh cylinder = build(checkCone({0, 0, 0}, 2, 2, 5, 64, true));
    CHECK_THAT(volume(cylinder), Catch::Matchers::WithinRel(3.141592653589793 * 4 * 5, 0.01));
    // Column 0 is the back of the model and its seam.
    CHECK(cylinder.vertices[0].position[0] == 0.f);
    CHECK(cylinder.vertices[0].position[2] == -2.f);
    CHECK(cylinder.vertices[64].position[2] == -2.f);
    CHECK(cylinder.vertices[0].uv[0] == 0.f);
    CHECK(cylinder.vertices[64].uv[0] > cylinder.vertices[63].uv[0]);
    CHECK_THROWS_AS(checkCone({0, 0, 0}, 0, 0, 1, 8, true), Violation);
    CHECK_THROWS_AS(checkCone({0, 0, 0}, 1, 1, 1, 2, true), Violation);
    CHECK_THROWS_AS(checkCone({0, 0, 0}, 1, 1, 1, 65, true), Violation);
    CHECK_THROWS_AS(checkCone({0, 0, 0}, 1, -1, 1, 8, true), Violation);
    CHECK_THROWS_AS(checkCone({0, 0, 0}, 1, 1, 0, 8, true), Violation);
}

TEST_CASE("a sphere") {
    for (const int sides : {3, 8, 16})
        for (const int rings : {2, 3, 8}) {
            INFO("sides " << sides << ", rings " << rings);
            checkEverywhere(checkSphere({0, 0, 0}, 3, sides, rings), true);
        }
    const Mesh mesh = build(checkSphere({0, 0, 0}, 3, 32, 16));
    CHECK_THAT(volume(mesh), Catch::Matchers::WithinRel(4.0 / 3.0 * 3.141592653589793 * 27, 0.02));
    // It stands on its anchor.
    f32 lowest = 1e9f;
    for (const Vertex& v : mesh.vertices) lowest = std::min(lowest, v.position[1]);
    CHECK(lowest == 0.f);
    CHECK_THROWS_AS(checkSphere({0, 0, 0}, 3, 8, 1), Violation);
    CHECK_THROWS_AS(checkSphere({0, 0, 0}, 0, 8, 4), Violation);
}

TEST_CASE("a lathe") {
    const auto barrel = points({{2, 0}, {3, 2}, {3.5f, 4}, {3, 6}, {2, 8}});
    checkEverywhere(checkLathe({0, 0, 0}, barrel, 12, nullptr), true);
    const f32 crease = 40.f;
    checkEverywhere(checkLathe({0, 0, 0}, barrel, 12, &crease), true);
    // A hard joint has two rows, a smooth one shares a row.
    CHECK(checkLathe({0, 0, 0}, barrel, 12, nullptr).rows.size() == 8);
    CHECK(checkLathe({0, 0, 0}, barrel, 12, &crease).rows.size() == 5);
    // Closed on the axis at both ends: no caps.
    const auto vase = points({{0, 0}, {3, 0}, {4, 5}, {1, 9}, {0, 9}});
    const LatheShape closed = checkLathe({0, 0, 0}, vase, 8, nullptr);
    checkEverywhere(closed, true);
    CHECK(count(closed).vertices == closed.rows.size() * 9);
    // A lid sunk into the top: the profile turns down and inward.
    const auto lidded = points({{3, 0}, {3, 8}, {2.5f, 8}, {2.5f, 7}, {0, 7}});
    checkEverywhere(checkLathe({0, 0, 0}, lidded, 10, nullptr), true);
    CHECK_THROWS_AS(checkLathe({0, 0, 0}, points({{1, 0}}), 8, nullptr), Violation);
    CHECK_THROWS_AS(checkLathe({0, 0, 0}, points({{1, 0}, {1, 0}}), 8, nullptr), Violation);
    CHECK_THROWS_AS(checkLathe({0, 0, 0}, points({{0, 0}, {0, 1}}), 8, nullptr), Violation);
    CHECK_THROWS_AS(checkLathe({0, 0, 0}, points({{-1, 0}, {1, 1}}), 8, nullptr), Violation);
    const f32 wide = 181.f;
    CHECK_THROWS_AS(checkLathe({0, 0, 0}, barrel, 8, &wide), Violation);
}

TEST_CASE("an extrusion") {
    const auto square = points({{0, 0}, {4, 0}, {4, 3}, {0, 3}});
    const ExtrudeShape plain = checkExtrude({0, 0, 0}, square, 2, nullptr);
    CHECK_THAT(volume(build(plain)), Catch::Matchers::WithinAbs(24.0, 1e-9));
    checkEverywhere(plain, true);
    // The same outline listed clockwise gives the same solid.
    const auto clockwise = points({{0, 0}, {0, 3}, {4, 3}, {4, 0}});
    CHECK_THAT(volume(build(checkExtrude({0, 0, 0}, clockwise, 2, nullptr))), Catch::Matchers::WithinAbs(24.0, 1e-9));
    // A concave outline needs real ear clipping.
    const auto arrow = points({{0, 0}, {4, 0}, {4, 2}, {6, 2}, {2, 6}, {-2, 2}, {0, 2}});
    checkEverywhere(checkExtrude({1, 1, 1}, arrow, 1.5f, nullptr), true);
    const auto notch = points({{0, 0}, {6, 0}, {6, 4}, {3, 1}, {0, 4}});
    checkEverywhere(checkExtrude({0, 0, 0}, notch, 1, nullptr), true);
    const f32 bevel = 0.25f;
    checkEverywhere(checkExtrude({0, 0, 0}, square, 2, &bevel), true);
    checkEverywhere(checkExtrude({0, 0, 0}, arrow, 1.5f, &bevel), true);
    CHECK(count(checkExtrude({0, 0, 0}, square, 2, &bevel)).vertices == 14 * 4);

    CHECK_THROWS_AS(checkExtrude({0, 0, 0}, points({{0, 0}, {1, 0}}), 1, nullptr), Violation);
    CHECK_THROWS_AS(checkExtrude({0, 0, 0}, points({{0, 0}, {1, 0}, {2, 0}}), 1, nullptr), Violation);
    CHECK_THROWS_AS(checkExtrude({0, 0, 0}, points({{0, 0}, {4, 4}, {4, 0}, {0, 4}}), 1, nullptr), Violation);
    CHECK_THROWS_AS(checkExtrude({0, 0, 0}, square, 0, nullptr), Violation);
    const f32 deep = 1.f;
    CHECK_THROWS_AS(checkExtrude({0, 0, 0}, square, 2, &deep), Violation);
    const f32 wide = 0.9f;
    CHECK_THROWS_AS(checkExtrude({0, 0, 0}, points({{0, 0}, {4, 0}, {4, 1}, {0, 1}}), 4, &wide), Violation);
}

TEST_CASE("quads and triangles") {
    checkEverywhere(checkQuad({0, 0, 0}, {4, 0, 0}, {4, 3, 0}, {0, 3, 0}), false);
    checkEverywhere(checkQuad({0, 0, 0}, {4, 0, -1}, {4, 3, -2}, {0, 3, 0}), false);
    checkEverywhere(checkTri({0, 0, 0}, {4, 0, 0}, {1, 3, 1}), false);
    const Mesh quad = build(checkQuad({0, 0, 0}, {4, 0, 0}, {4, 3, 0}, {0, 3, 0}));
    CHECK(quad.vertices[0].normal[2] == 1.f);
    CHECK(quad.vertices[3].uv[0] == 0.f);
    CHECK(quad.vertices[3].uv[1] == 0.f);
    Mesh mesh;
    const Affine identity;
    CHECK_THROWS_AS(emit(Target{mesh, identity, repeat(), 1}, checkQuad({0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0})),
                    Violation);
}

TEST_CASE("placements compose and stay exact at quarter turns") {
    // A quarter turn about +Y sends +Z to +X, exactly.
    const Affine quarter = placed(kPlaceYaw, {}, 90.f, 0, 0, 1, 0);
    CHECK(quarter.a[0][2] == 1.0);
    CHECK(quarter.a[2][2] == 0.0);
    CHECK(quarter.a[2][0] == -1.0);
    // A positive pitch sends +Y toward +Z.
    const Affine pitch = placed(kPlacePitch, {}, 0, 90.f, 0, 1, 0);
    CHECK(pitch.a[2][1] == 1.0);
    CHECK(pitch.a[1][1] == 0.0);
    // Nesting is the product of the two maps.
    Placement inner;
    inner.mask = kPlaceAt | kPlaceYaw;
    inner.at = {1, 0, 0};
    inner.yaw = 90.f;
    const Affine outer = placed(kPlaceAt | kPlaceYaw, {0, 5, 0}, 90.f, 0, 0, 1, 0);
    const Affine both = place(outer, inner);
    CHECK(both.a[0][0] == -1.0); // two quarter turns
    CHECK(both.o[0] == 0.0);
    CHECK(both.o[1] == 5.0);
    CHECK(both.o[2] == -1.0); // the inner offset, turned by the outer map
    CHECK_FALSE(both.flipped);
    CHECK(placed(kPlaceMirrorX, {}, 0, 0, 0, 1, 1).flipped);

    Placement bad;
    bad.mask = kPlaceScale;
    bad.scale[0] = bad.scale[1] = bad.scale[2] = 0.f;
    CHECK_THROWS_AS(place(Affine{}, bad), Violation);
    bad = {};
    bad.mask = kPlaceMirrorX;
    bad.mirror[0] = 2;
    CHECK_THROWS_AS(place(Affine{}, bad), Violation);
    bad = {};
    bad.mask = kPlaceYaw;
    bad.yaw = INFINITY;
    CHECK_THROWS_AS(place(Affine{}, bad), Violation);
}

TEST_CASE("a mirrored scope flips winding and the tangent sign") {
    const BoxShape box = checkBox({2, 0, 0}, {1, 1, 1});
    const Mesh plain = build(box);
    const Mesh mirrored = build(box, placed(kPlaceMirrorX, {}, 0, 0, 0, 1, 1));
    CHECK(plain.vertices[0].tangent[3] == 1.f);
    CHECK(mirrored.vertices[0].tangent[3] == -1.f);
    CHECK(mirrored.vertices[0].position[0] == -plain.vertices[0].position[0]);
    CHECK(mirrored.triangles[0].b == plain.triangles[0].c);
    CHECK_THAT(volume(mirrored), Catch::Matchers::WithinAbs(1.0, 1e-9));
}

TEST_CASE("emission is deterministic") {
    const auto arrow = points({{0, 0}, {4, 0}, {4, 2}, {6, 2}, {2, 6}, {-2, 2}, {0, 2}});
    const f32 bevel = 0.25f;
    const auto shape = checkExtrude({1, 1, 1}, arrow, 1.5f, &bevel);
    const Affine scope = scopes()[2];
    const Mesh first = build(shape, scope), second = build(shape, scope);
    REQUIRE(first.vertices.size() == second.vertices.size());
    CHECK(std::memcmp(first.vertices.data(), second.vertices.data(), first.vertices.size() * sizeof(Vertex)) == 0);
}

TEST_CASE("vec3 values") {
    CHECK(vec3Add({1, 2, 3}, {4, 5, 6}) == Vec3{5, 7, 9});
    CHECK(vec3Cross({1, 0, 0}, {0, 1, 0}) == Vec3{0, 0, 1});
    CHECK(vec3Dot({1, 2, 3}, {4, 5, 6}) == 32.f);
    CHECK(vec3Len({3, 4, 12}) == 13.f);
    CHECK(vec3Normalize({0, 0, 0}) == Vec3{0, 0, 0});
    CHECK(vec3Normalize({0, 5, 0}) == Vec3{0, 1, 0});
    CHECK(vec3Mix({0, 0, 0}, {2, 4, 6}, 0.5f) == Vec3{1, 2, 3});
    CHECK(vec3Mix({0, 0, 0}, {2, 4, 6}, 7.f) == Vec3{2, 4, 6});
}

// ---- shapes along a path (M3) ---------------------------------------------

namespace {
std::vector<Vec3> path(std::initializer_list<Vec3> list) { return list; }
} // namespace

TEST_CASE("a straight tube is a cone") {
    // Control points on a vertical line: the rings lie where a cone's do.
    const auto tube = checkTube({0, 0, 0}, {0, 3, 0}, {0, 6, 0}, {0, 9, 0}, 2.f, 1.f, 8, 3);
    const auto cone = checkCone({0, 0, 0}, 2.f, 1.f, 9.f, 8, true);
    const Mesh rings = build(tube), solid = build(cone);
    for (u32 j = 0; j <= 8; ++j) {
        INFO("column " << j);
        for (int axis = 0; axis < 3; ++axis) {
            CHECK_THAT(rings.vertices[j].position[axis], Catch::Matchers::WithinAbs(solid.vertices[j].position[axis], 1e-6));
            CHECK_THAT(rings.vertices[j].normal[axis], Catch::Matchers::WithinAbs(solid.vertices[j].normal[axis], 1e-6));
            CHECK_THAT(rings.vertices[j].tangent[axis], Catch::Matchers::WithinAbs(solid.vertices[j].tangent[axis], 1e-6));
        }
        CHECK_THAT(rings.vertices[j].uv[0], Catch::Matchers::WithinAbs(solid.vertices[j].uv[0], 1e-6));
    }
    checkEverywhere(tube, true);
}

TEST_CASE("a tube along a curve") {
    const auto arm = checkTube({0, 0, 0}, {0, 20, 0}, {12, 24, 0}, {20, 16, 0}, 3.f, 2.f, 10, 12);
    CHECK(count(arm).vertices == 13 * 11 + 2 * 11);
    CHECK(count(arm).triangles == 12 * 2 * 10 + 2 * 10);
    checkEverywhere(arm, true);
    // A curve in space, and one whose end control points sit on its ends.
    checkEverywhere(checkTube({0, 0, 0}, {5, 5, 5}, {-5, 10, 0}, {0, 15, -5}, 1.f, 1.f, 6, 16), true);
    checkEverywhere(checkTube({0, 0, 0}, {0, 0, 0}, {10, 10, 0}, {10, 10, 0}, 1.f, 1.f, 5, 8), true);
    // A pointed end, as a cone's.
    checkEverywhere(checkTube({0, 0, 0}, {0, 5, 0}, {5, 10, 0}, {10, 10, 0}, 2.f, 0.f, 8, 6), true);
    // The ends of the curve are its first and last control points.
    const Mesh mesh = build(arm);
    const u32 lastRing = 12 * 11;
    double x = 0, y = 0;
    for (u32 j = 0; j < 10; ++j) {
        x += mesh.vertices[lastRing + j].position[0];
        y += mesh.vertices[lastRing + j].position[1];
    }
    CHECK_THAT(x / 10.0, Catch::Matchers::WithinAbs(20.0, 1e-4));
    CHECK_THAT(y / 10.0, Catch::Matchers::WithinAbs(16.0, 1e-4));
}

TEST_CASE("tube arguments are checked") {
    CHECK_THROWS_WITH(checkTube({0, 0, 0}, {0, 1, 0}, {0, 2, 0}, {0, 3, 0}, 0.f, 0.f, 8, 4), "radius must be positive");
    CHECK_THROWS_WITH(checkTube({0, 0, 0}, {0, 1, 0}, {0, 2, 0}, {0, 3, 0}, 1.f, -1.f, 8, 4), "radius must be positive");
    CHECK_THROWS_WITH(checkTube({0, 0, 0}, {0, 1, 0}, {0, 2, 0}, {0, 3, 0}, 1.f, 1.f, 2, 4), "sides must be in 3..64");
    CHECK_THROWS_WITH(checkTube({0, 0, 0}, {0, 1, 0}, {0, 2, 0}, {0, 3, 0}, 1.f, 1.f, 8, 0), "segments must be in 1..256");
    CHECK_THROWS_WITH(checkTube({0, 0, 0}, {0, 1, 0}, {0, 2, 0}, {0, 3, 0}, 1.f, 1.f, 8, 257), "segments must be in 1..256");
    CHECK_THROWS_WITH(checkTube({1, 1, 1}, {1, 1, 1}, {1, 1, 1}, {1, 1, 1}, 1.f, 1.f, 8, 4), "tube curve has no direction");
    const f32 inf = std::numeric_limits<f32>::infinity();
    CHECK_THROWS_WITH(checkTube({0, 0, 0}, {0, inf, 0}, {0, 2, 0}, {0, 3, 0}, 1.f, 1.f, 8, 4), "tube needs finite arguments");
}

TEST_CASE("a sweep") {
    const auto square = points({{-1, -1}, {1, -1}, {1, 1}, {-1, 1}});
    // A rail up, across and down: mitred twice. A centred profile keeps
    // the volume of its area times the length of the centre line.
    const auto rail = checkSweep(path({{0, 0, 0}, {0, 10, 0}, {8, 10, 0}, {8, 0, 0}}), square);
    CHECK(count(rail).vertices == 2 * 4 + 4 * 4 * 3);
    CHECK(count(rail).triangles == 2 * 2 + 2 * 4 * 3);
    CHECK_THAT(volume(build(rail)), Catch::Matchers::WithinAbs(4.0 * 28.0, 1e-9));
    checkEverywhere(rail, true);
    // A profile given clockwise, a path in space and a shallow turn.
    checkEverywhere(checkSweep(path({{0, 0, 0}, {3, 4, 5}, {6, 4, 0}, {9, 9, 9}}),
                               points({{0, 0}, {0, 1}, {2, 1}, {2, 0}})), true);
    checkEverywhere(checkSweep(path({{0, 0, 0}, {10, 0, 0}, {20, 0.5f, 0}}), points({{-1, 0}, {1, 0}, {0, 2}})), true);
    // An L profile, not convex: the caps are triangulated like an extrude.
    checkEverywhere(checkSweep(path({{0, 0, 0}, {0, 0, 10}}), points({{0, 0}, {3, 0}, {3, 1}, {1, 1}, {1, 3}, {0, 3}})), true);
    // On a horizontal path the profile stands upright: its y is +Y.
    const Mesh flat = build(checkSweep(path({{0, 0, 0}, {10, 0, 0}}), points({{0, 0}, {1, 0}, {1, 2}, {0, 2}})));
    double top = -1e9;
    for (const Vertex& v : flat.vertices) top = std::max(top, static_cast<double>(v.position[1]));
    CHECK(top == 2.0);
}

TEST_CASE("sweep arguments are checked") {
    const auto square = points({{-1, -1}, {1, -1}, {1, 1}, {-1, 1}});
    CHECK_THROWS_WITH(checkSweep(path({{0, 0, 0}}), square), "sweep path needs 2..256 points");
    CHECK_THROWS_WITH(checkSweep(path({{0, 0, 0}, {0, 0, 0}}), square), "sweep path repeats a point");
    CHECK_THROWS_WITH(checkSweep(path({{0, 0, 0}, {0, 5, 0}, {0, 0, 0}}), square), "sweep path turns back on itself");
    CHECK_THROWS_WITH(checkSweep(path({{0, 0, 0}, {0, 5, 0}}), points({{0, 0}, {1, 0}})), "sweep profile needs 3..256 points");
    CHECK_THROWS_WITH(checkSweep(path({{0, 0, 0}, {0, 5, 0}}), points({{0, 0}, {1, 0}, {2, 0}})), "sweep profile has no area");
    CHECK_THROWS_WITH(checkSweep(path({{0, 0, 0}, {0, 5, 0}}), points({{0, 0}, {4, 0}, {4, 4}, {3, -1}, {1, 4}})),
                      "sweep profile is self-intersecting");
}

// ---- shaping blocks (M3) --------------------------------------------------

#include "iprop/model/Blocks.hpp"

namespace {

// A shape built into a block's buffer, processed, then passed to a mesh.
template<class Shape, class Process>
Mesh shaped(const Shape& shape, Process process, const Affine& scope = {}) {
    LocalMesh block;
    emit(Target{block, Affine{}, repeat(), 1}, shape);
    checkFinite(block);
    process(block);
    Mesh mesh;
    pass(Target{mesh, scope, repeat(), 0}, block);
    return mesh;
}

} // namespace

TEST_CASE("a block passes its buffer unchanged when it does nothing") {
    const auto box = checkBox({1, 2, 3}, {4, 6, 2}, 0.5f);
    const Mesh direct = build(box);
    const Mesh passed = shaped(box, [](LocalMesh&) {});
    REQUIRE(passed.vertices.size() == direct.vertices.size());
    for (std::size_t i = 0; i < direct.vertices.size(); ++i) {
        CHECK(std::memcmp(direct.vertices[i].position, passed.vertices[i].position, 12) == 0);
        CHECK(std::memcmp(direct.vertices[i].uv, passed.vertices[i].uv, 8) == 0);
        for (int axis = 0; axis < 3; ++axis)
            CHECK_THAT(passed.vertices[i].normal[axis], Catch::Matchers::WithinAbs(direct.vertices[i].normal[axis], 1e-7));
    }
    // Neutral arguments change nothing either.
    const Mesh neutral = shaped(box, [](LocalMesh& mesh) {
        smooth(mesh, 0.f);
        refine(mesh, 1);
        for (const DeformKind kind : {DeformKind::Bend, DeformKind::Twist})
            deform(mesh, deformation(kind, 0.f, mesh));
        deform(mesh, deformation(DeformKind::Taper, 1.f, mesh));
    });
    REQUIRE(neutral.vertices.size() == direct.vertices.size());
    for (std::size_t i = 0; i < direct.vertices.size(); ++i)
        CHECK(std::memcmp(direct.vertices[i].position, neutral.vertices[i].position, 12) == 0);
}

TEST_CASE("refine splits every triangle and keeps the solid closed") {
    const auto box = checkBox({0, 0, 0}, {4, 6, 2});
    LocalMesh block;
    emit(Target{block, Affine{}, repeat(), 1}, box);
    const Counts counts = refineCounts(block, 3);
    // Each face has its own four vertices: 6 faces of 5 edges (4 sides and
    // a diagonal), 2 points on each edge and 1 inside each triangle.
    CHECK(counts.vertices == 30 * 2 + 12 * 1);
    CHECK(counts.triangles == 12 * 9);
    for (const u32 n : {2u, 3u, 5u}) {
        INFO("refine " << n);
        const Mesh mesh = shaped(box, [n](LocalMesh& m) { refine(m, n); });
        CHECK(mesh.triangles.size() == 12 * n * n);
        checkSurface(mesh);
        checkClosed(mesh);
        CHECK_THAT(volume(mesh), Catch::Matchers::WithinAbs(48.0, 1e-9));
    }
    checkClosed(shaped(checkBox({0, 0, 0}, {1, 1, 1}, 0.2f), [](LocalMesh& m) { refine(m, 4); }));
}

TEST_CASE("displace moves copies of a vertex together: no cracks") {
    const auto box = checkBox({0, 0, 0}, {20, 20, 20});
    for (const u32 seed : {1u, 7u, 99u}) {
        INFO("seed " << seed);
        const Mesh rock = shaped(box, [seed](LocalMesh& m) {
            refine(m, 6);
            displace(m, 3.f, 0.15f, seed);
        });
        checkClosed(rock);
        for (const Vertex& v : rock.vertices)
            REQUIRE_THAT(norm(normal(v)), Catch::Matchers::WithinAbs(1.0, 1e-6));
        for (const Vertex& v : rock.vertices) REQUIRE(std::fabs(dot(normal(v), tangent(v))) < 1e-5);
    }
    // The same seed moves the same way; another seed another way.
    const auto rock = [&](u32 seed) {
        return shaped(box, [seed](LocalMesh& m) {
            refine(m, 3);
            displace(m, 3.f, 0.15f, seed);
        });
    };
    const Mesh a = rock(5), b = rock(5), c = rock(6);
    CHECK(std::memcmp(a.vertices.data(), b.vertices.data(), a.vertices.size() * sizeof(Vertex)) == 0);
    CHECK(std::memcmp(a.vertices.data(), c.vertices.data(), a.vertices.size() * sizeof(Vertex)) != 0);
    // Noise in space stays in about -1..1 and is a lattice of zeros.
    CHECK(noise3(3.0, -2.0, 7.0, 11) == 0.0);
    for (int i = 0; i < 1000; ++i) {
        const double value = noise3(i * 0.37, i * 0.11 - 5.0, i * 0.23, 3);
        REQUIRE(std::fabs(value) <= 1.1);
    }
    CHECK_THROWS_WITH(noise3(3e9, 0.0, 0.0, 1), "noise coordinate out of range");
}

TEST_CASE("bend, twist and taper keep the surface sound") {
    const auto column = checkCone({0, 0, 0}, 2.f, 2.f, 30.f, 12, true);
    for (const auto& [kind, parameter] : std::vector<std::pair<DeformKind, f32>>{
             {DeformKind::Bend, 90.f}, {DeformKind::Bend, -120.f}, {DeformKind::Twist, 270.f},
             {DeformKind::Taper, 0.25f}, {DeformKind::Taper, 3.f}}) {
        INFO("kind " << static_cast<int>(kind) << " " << parameter);
        int index = 0;
        for (const Affine& scope : scopes()) {
            INFO("scope " << index++);
            const Mesh mesh = shaped(column, [&](LocalMesh& m) {
                refine(m, 4);
                deform(m, deformation(kind, parameter, m));
            }, scope);
            checkSurface(mesh);
            checkClosed(mesh);
        }
    }
    // A bend of 90 degrees over 30 units turns the top of the axis onto an
    // arc of radius 30 / (pi/2).
    const auto rod = checkBox({0, 0, 0}, {1, 30, 1});
    LocalMesh block;
    emit(Target{block, Affine{}, repeat(), 1}, rod);
    const Deformation bend = deformation(DeformKind::Bend, 90.f, block);
    const Point3 tip = deformPoint(bend, {0.0, 30.0, 0.0});
    const double radius = 30.0 / (3.141592653589793 / 2.0);
    CHECK_THAT(tip.x, Catch::Matchers::WithinAbs(radius, 1e-9));
    CHECK_THAT(tip.y, Catch::Matchers::WithinAbs(radius, 1e-9));
    // Points beyond the centre fold: a vertex there traps, a point does not.
    LocalMesh wide;
    emit(Target{wide, Affine{}, repeat(), 1}, checkBox({30, 0, 0}, {20, 10, 1}));
    CHECK_THROWS_WITH(deform(wide, deformation(DeformKind::Bend, 360.f, wide)), "bend is too tight for its geometry");
    CHECK_NOTHROW(deformPoint(deformation(DeformKind::Bend, 360.f, wide), {100.0, 5.0, 0.0}));
}

TEST_CASE("smooth averages normals across soft edges only") {
    // A lathe of hard joints: smoothing at 60 degrees joins the rows of
    // a 30 degree joint and keeps the 90 degree rim hard.
    const auto profile = points({{2, 0}, {2, 4}, {3, 6}, {0, 6}});
    const auto lathe = checkLathe({0, 0, 0}, profile, 12, nullptr);
    const Mesh hard = build(lathe);
    const Mesh soft = shaped(lathe, [](LocalMesh& m) { smooth(m, 60.f); });
    REQUIRE(soft.vertices.size() == hard.vertices.size());
    checkSurface(soft);
    std::size_t changed = 0;
    for (std::size_t i = 0; i < hard.vertices.size(); ++i)
        if (std::fabs(hard.vertices[i].normal[1] - soft.vertices[i].normal[1]) > 1e-4) ++changed;
    CHECK(changed > 0);
    // A box keeps its edges below 90 degrees, and rounds them at 100.
    const auto box = checkBox({0, 0, 0}, {2, 2, 2});
    const Mesh kept = shaped(box, [](LocalMesh& m) { smooth(m, 89.f); });
    for (const Vertex& v : kept.vertices) CHECK(std::fabs(v.normal[0]) + std::fabs(v.normal[1]) + std::fabs(v.normal[2]) == 1.f);
    const Mesh round = shaped(box, [](LocalMesh& m) { smooth(m, 100.f); });
    for (const Vertex& v : round.vertices) CHECK_THAT(std::fabs(v.normal[0]), Catch::Matchers::WithinAbs(1.0 / std::sqrt(3.0), 1e-6));
}
