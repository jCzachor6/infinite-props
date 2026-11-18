#pragma once

// The sculpt block (spec models v1.2, M6): lumps joined into one smooth
// surface. The field of the lumps is sampled on a grid and meshed with
// surface nets, and the skin is laid on by three planar projections that
// are written into the UVs.

#include "iprop/model/Geometry.hpp"
#include "iprop/model/Ops.hpp"

#include <array>
#include <span>
#include <vector>

namespace iprop::model {

enum class LumpKind : u8 { Ball, Capsule, Slab };

// A lump in the frame of its sculpt block. A ball has a centre `a`, its own
// axes and a radius along each; a capsule runs from `a` to `b` with a radius
// at each end; a slab is a box around `a` with half sizes along its axes and
// edges rounded by `round`. A carved lump is cut out of what is there.
struct Lump {
    LumpKind kind = LumpKind::Ball;
    bool carve = false;
    Point3 a, b;
    Point3 axes[3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    double radii[3] = {0, 0, 0};
    double round = 0;
};

// The value of the field at a point: negative inside. Lumps apply in order:
// one joins by a smooth minimum of radius `blend`, a carved one is cut out
// by the same smooth maximum. With no lump the field is +infinity.
double sculptField(std::span<const Lump> lumps, double blend, const Point3& p);

// The nodes a sculpt samples: node (i, j, k) lies at (origin + {i, j, k}) *
// cell, so the grid does not move with the lumps. No nodes when no lump
// adds anything.
struct SculptGrid {
    i64 origin[3] = {0, 0, 0};
    u32 nodes[3] = {0, 0, 0};
    u64 samples() const { return static_cast<u64>(nodes[0]) * nodes[1] * nodes[2]; }
};
// Throws a Violation when the grid would exceed kMaxSculptCells on an axis.
SculptGrid sculptGrid(std::span<const Lump> lumps, double cell, double blend);

// The field at every node, in z, y, x order.
std::vector<double> sculptSamples(std::span<const Lump> lumps, double blend, double cell, const SculptGrid& grid);

// What the net of a sampled grid will hold, found without building it.
struct SculptCounts {
    u64 points = 0, triangles = 0;
};
SculptCounts sculptCount(const SculptGrid& grid, std::span<const double> samples);
// The surface before its skin: one point for each cell the surface crosses
// and two triangles for each grid edge it crosses, counterclockwise from
// outside. `projection` is the plane a triangle is mapped by: its axis
// twice, plus one when it faces the negative side. `vertices` is what the
// mesh will hold: a point once for each projection that uses it.
struct SculptNet {
    std::vector<Point3> points;
    std::vector<std::array<u32, 3>> triangles;
    std::vector<u8> projection;
    u32 vertices = 0;
};
SculptNet sculptNet(const SculptGrid& grid, std::span<const double> samples, double cell);

// Appends the net to a block's buffer with its normals, its UVs under
// `mapping` and its tangents.
void sculptMesh(LocalMesh& mesh, const SculptNet& net, std::span<const Lump> lumps, double blend, double cell,
                const Mapping& mapping, u32 skin);

} // namespace iprop::model
