#pragma once

// The shaping blocks of the model VM (spec models v1.2, M3): smooth,
// refine, bend, twist, taper and displace. Each works on the buffer of a
// block that closes, in the block's own frame and in binary64, before the
// buffer passes to the block's parent. Like the primitives they know nothing
// of the interpreter; a broken rule throws a Violation.

#include "iprop/model/Geometry.hpp"

namespace iprop::model {

// Every position of a buffer is finite before it is processed.
void checkFinite(const LocalMesh& mesh);

// Vertices at equal positions average their normals with those within
// `degrees` of their own; tangents are made perpendicular again.
void smooth(LocalMesh& mesh, f32 degrees);

// What refine(n) makes of a buffer: the vertices it adds and the triangles
// there are afterwards.
Counts refineCounts(const LocalMesh& mesh, u32 n);
// Splits every triangle into n*n, sharing the points of shared edges.
void refine(LocalMesh& mesh, u32 n);

enum class DeformKind : u8 { Bend, Twist, Taper };

// A deformation along the Y axis of a block, over the Y extent of its
// vertices. A neutral argument, an empty block or a flat one is the identity.
struct Deformation {
    DeformKind kind = DeformKind::Bend;
    bool identity = true;
    double y0 = 0.0, length = 0.0;
    double a = 0.0;      // the angle in radians (bend, twist) or the factor (taper)
    double radius = 0.0; // a bend: the radius of the arc the Y axis becomes
};
Deformation deformation(DeformKind kind, f32 parameter, const LocalMesh& mesh);
// Moves the vertices; normals follow the cofactors of the Jacobian and
// tangents the Jacobian. A bend that would fold a vertex over its centre
// traps.
void deform(LocalMesh& mesh, const Deformation& deformation);
// An output point inside the block: moved by the same map, never trapping.
Point3 deformPoint(const Deformation& deformation, const Point3& p);

// Moves each vertex along the summed normals of its position by
// amount * noise3(position * frequency, seed), then derives the normals from
// the moved triangles.
void displace(LocalMesh& mesh, f32 amount, f32 frequency, u32 seed);

// Gradient noise in space, about -1 to 1 (spec models v1.2, Noise in space).
double noise3(double x, double y, double z, u32 seed);

} // namespace iprop::model
