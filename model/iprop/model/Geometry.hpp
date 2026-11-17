#pragma once

// The geometry kernels of the model VM (spec models v1.2): placement,
// vertex emission and the primitives. They know nothing of the interpreter:
// a primitive is checked into a shape, counted, and emitted into a mesh.
// Everything here is binary64 in the written order, rounded to binary32 once
// per stored component, so every host produces the same bits.

#include "iprop/Error.hpp"
#include "iprop/Types.hpp"

#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace iprop::model {

// A rule a kernel found broken; the VM reports it as a trap of this reason.
struct Violation : std::runtime_error {
    ErrorReason reason;
    explicit Violation(std::string message, ErrorReason cause = ErrorReason::NumericRange)
        : std::runtime_error(std::move(message)), reason(cause) {}
};

struct Vertex {
    f32 position[3];
    f32 normal[3];
    f32 tangent[4]; // xyz and the sign of the bitangent
    f32 uv[2];
};
static_assert(sizeof(Vertex) == 48, "a vertex is charged 48 bytes");

struct Triangle {
    u32 a, b, c;
    u32 skin; // the record its primitive was given
};

struct Mesh {
    std::vector<Vertex> vertices;
    std::vector<u32> vertexSkins; // parallel to vertices
    std::vector<Triangle> triangles;
};

struct Point2 {
    double x = 0, y = 0;
};
struct Point3 {
    double x = 0, y = 0, z = 0;
};

// The map of a scope, p -> a*p + o, with the cofactors of `a` for normals
// and whether an odd number of mirrors lies on the way from the root.
struct Affine {
    double a[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    double o[3] = {0, 0, 0};
    double c[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    bool flipped = false;
};

// The arguments of place.begin; `mask` says which are present.
struct Placement {
    u16 mask = 0;
    Vec3 at{};
    f32 yaw = 0.f, pitch = 0.f, roll = 0.f;
    f32 scale[3] = {1.f, 1.f, 1.f};
    i32 mirror[3] = {0, 0, 0};
};
// Checks the arguments in the specified order and returns the child scope.
Affine place(const Affine& parent, const Placement& placement);

// How face coordinates become UVs: the skin record and its texture.
struct Mapping {
    bool fit = false;
    double density = 1.0;
    double shiftU = 0.0, shiftV = 0.0;
    double assetWidth = 1.0, assetHeight = 1.0;
};

// Where a primitive writes.
struct Target {
    Mesh& mesh;
    const Affine& scope;
    Mapping mapping;
    u32 skin = 0;
};

struct Counts {
    u32 vertices = 0;
    u32 triangles = 0;
};

// ---- primitives -------------------------------------------------------------
// check*(arguments) validates and prepares; count() is exact; emit() appends
// exactly that many vertices and triangles. Only a coordinate that leaves the
// binary32 range can fail in emit().

struct BoxShape {
    Point3 at;
    double w = 0, h = 0, d = 0;
    double bevel = 0; // zero: a plain box
};
BoxShape checkBox(Vec3 at, Vec3 size);
BoxShape checkBox(Vec3 at, Vec3 size, f32 bevel);
Counts count(const BoxShape& shape);
void emit(const Target& target, const BoxShape& shape);

struct ConeShape {
    Point3 at;
    double r0 = 0, r1 = 0, height = 0;
    u32 sides = 0;
    bool caps = true;
};
ConeShape checkCone(Vec3 at, f32 r0, f32 r1, f32 height, i32 sides, bool caps);
Counts count(const ConeShape& shape);
void emit(const Target& target, const ConeShape& shape);

struct SphereShape {
    Point3 at;
    double radius = 0;
    u32 sides = 0, rings = 0;
};
SphereShape checkSphere(Vec3 at, f32 radius, i32 sides, i32 rings);
Counts count(const SphereShape& shape);
void emit(const Target& target, const SphereShape& shape);

struct LatheShape {
    Point3 at;
    std::vector<Point2> profile; // (radius, height)
    u32 sides = 0;
    // One row of vertices around the axis.
    struct Row {
        std::size_t point = 0;
        double nr = 0, ny = 0; // the profile normal
    };
    std::vector<Row> rows;
    struct Segment {
        std::size_t lower = 0, upper = 0; // rows
    };
    std::vector<Segment> segments;
    std::vector<double> cumulative; // profile length up to each point
    double maxRadius = 0;
};
// `crease` is null for the form without one (every joint hard).
LatheShape checkLathe(Vec3 at, std::span<const Vec2> profile, i32 sides, const f32* crease);
Counts count(const LatheShape& shape);
void emit(const Target& target, const LatheShape& shape);

struct ExtrudeShape {
    Point3 at;
    std::vector<Point2> outline; // counterclockwise
    std::vector<Point2> inset;   // with a bevel: the cap outline
    std::vector<u32> triangles;  // of the cap outline, three per triangle
    double depth = 0;
    double bevel = 0; // zero: no bevel
};
// `bevel` is null for the form without one.
ExtrudeShape checkExtrude(Vec3 at, std::span<const Vec2> outline, f32 depth, const f32* bevel);
Counts count(const ExtrudeShape& shape);
void emit(const Target& target, const ExtrudeShape& shape);

struct QuadShape {
    Point3 a, b, c, d;
};
QuadShape checkQuad(Vec3 a, Vec3 b, Vec3 c, Vec3 d);
Counts count(const QuadShape& shape);
void emit(const Target& target, const QuadShape& shape);

struct TriShape {
    Point3 a, b, c;
};
TriShape checkTri(Vec3 a, Vec3 b, Vec3 c);
Counts count(const TriShape& shape);
void emit(const Target& target, const TriShape& shape);

// ---- vec3 values (binary32, like the vec2 operations) -----------------------

Vec3 vec3Add(Vec3 a, Vec3 b);
Vec3 vec3Sub(Vec3 a, Vec3 b);
Vec3 vec3Mul(Vec3 a, Vec3 b);
Vec3 vec3Div(Vec3 a, Vec3 b);
Vec3 vec3Scale(Vec3 a, f32 k);
Vec3 vec3Neg(Vec3 a);
f32 vec3Len(Vec3 a);
f32 vec3Dot(Vec3 a, Vec3 b);
Vec3 vec3Cross(Vec3 a, Vec3 b);
// Finite components only; the VM checks before calling.
Vec3 vec3Normalize(Vec3 a);
Vec3 vec3Mix(Vec3 a, Vec3 b, f32 t);

// The pinned sine on a binary64 argument, before its final cast (spec
// models v1.2, Conventions); exposed for the reference tests.
double sin64(double x);

} // namespace iprop::model
