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
    // skin.region (v1.2, M2): each face shows this window of the image once,
    // in asset units; implies fit. A face coordinate outside its face is
    // clamped to the face, so no face samples beyond its window.
    bool region = false;
    double regionX = 0.0, regionY = 0.0, regionW = 1.0, regionH = 1.0;
};

// A vertex of a block's own buffer (v1.2, M3): kept in binary64 in the
// block's frame until the block passes it to its parent; its UV is final.
struct LocalVertex {
    Point3 position, normal, tangent;
    double w = 1.0; // the bitangent sign
    f32 uv[2] = {0.f, 0.f};
    u32 skin = 0;
};

struct LocalMesh {
    std::vector<LocalVertex> vertices;
    std::vector<Triangle> triangles;
};

// Where a primitive writes: the model's mesh, rounded to binary32 once, or
// the buffer of an open block, kept in binary64.
struct Target {
    Target(Mesh& root, const Affine& placement, Mapping map, u32 record)
        : mesh(&root), scope(placement), mapping(map), skin(record) {}
    Target(LocalMesh& block, const Affine& placement, Mapping map, u32 record)
        : local(&block), scope(placement), mapping(map), skin(record) {}
    Mesh* mesh = nullptr;
    LocalMesh* local = nullptr;
    const Affine& scope;
    Mapping mapping;
    u32 skin = 0;
    u32 vertexCount() const {
        return static_cast<u32>(mesh ? mesh->vertices.size() : local->vertices.size());
    }
};

// The image of a vertex under a scope, before any rounding (spec models
// v1.2, Placement scopes): the point, the unit normal through the cofactors,
// the unit tangent and the sign, negated in a flipped scope.
struct PlacedVertex {
    double position[3] = {0.0, 0.0, 0.0};
    Point3 normal, tangent;
    double w = 1.0;
};
PlacedVertex transform(const Affine& scope, const Point3& p, const Point3& n, const Point3& t, double w);
// The point and vector maps of a scope alone: A·p + o and A·v, with the
// arithmetic of a vertex position.
Point3 mapPoint(const Affine& scope, const Point3& p);
Point3 mapVector(const Affine& scope, const Point3& v);
// Whether turn() takes its exact path: a multiple of 90 degrees.
bool quarterTurn(f32 degrees);

// Passes the vertices and triangles of a closed block to its parent through
// the scope the block was opened in, keeping each vertex's UV and skin.
void pass(const Target& target, const LocalMesh& block);

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

// ---- shapes along a path (M3) ---------------------------------------------

// A tube along a cubic Bezier curve, its radius running from r0 to r1. The
// rings are sampled at even parameter steps and carry rotation-minimising
// frames: U and V span a ring, U x V = T, the direction of the curve.
struct TubeShape {
    std::vector<Point3> centres, directions, us, vs;
    std::vector<double> radii, cumulative; // cumulative: chord length up to each ring
    double length = 0, maxRadius = 0, r0 = 0, r1 = 0;
    u32 sides = 0, segments = 0;
};
TubeShape checkTube(Vec3 a, Vec3 b, Vec3 c, Vec3 d, f32 r0, f32 r1, i32 sides, i32 segments);
Counts count(const TubeShape& shape);
void emit(const Target& target, const TubeShape& shape);

// A profile swept along a polyline, mitred at every inner point.
struct SweepShape {
    std::vector<Point3> path;
    std::vector<Point2> profile;   // counterclockwise
    std::vector<u32> triangles;    // of the profile, three per triangle
    std::vector<Point3> ringU, ringV;               // the profile's axes at each path point
    std::vector<Point3> segmentU, segmentV, segmentD; // the frame of each segment
    std::vector<double> cumulative;                  // path length up to each point
};
SweepShape checkSweep(std::span<const Vec3> path, std::span<const Vec2> profile);
Counts count(const SweepShape& shape);
void emit(const Target& target, const SweepShape& shape);

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
