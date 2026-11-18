#include "iprop/model/Sculpt.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

// Every expression below is written in the order the specification gives:
// one IEEE operation per operator, no contraction. Nodes, cells, edges and
// vertices are visited in rising z, then y, then x.

namespace iprop::model {
namespace {

[[noreturn]] void violate(const char* message, ErrorReason reason = ErrorReason::NumericRange) {
    throw Violation(message, reason);
}

// What a lump is charged covers what it holds.
static_assert(sizeof(Lump) <= kLumpBytes);

Point3 add(const Point3& a, const Point3& b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
Point3 sub(const Point3& a, const Point3& b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
Point3 scaled(const Point3& v, double k) { return {v.x * k, v.y * k, v.z * k}; }
double dot(const Point3& a, const Point3& b) { return (a.x * b.x + a.y * b.y) + a.z * b.z; }
Point3 cross(const Point3& a, const Point3& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
double length(const Point3& v) { return std::sqrt(dot(v, v)); }

// The distance to one lump, negative inside. It is the distance itself for
// every lump but a ball of unequal radii, whose value is close to it near
// the surface and never above it far away.
double lumpDistance(const Lump& lump, const Point3& p) {
    switch (lump.kind) {
    case LumpKind::Ball: {
        const Point3 d = sub(p, lump.a);
        const Point3 q{dot(lump.axes[0], d), dot(lump.axes[1], d), dot(lump.axes[2], d)};
        if (lump.radii[0] == lump.radii[1] && lump.radii[1] == lump.radii[2]) return length(q) - lump.radii[0];
        const Point3 unit{q.x / lump.radii[0], q.y / lump.radii[1], q.z / lump.radii[2]};
        const Point3 slope{unit.x / lump.radii[0], unit.y / lump.radii[1], unit.z / lump.radii[2]};
        const double k0 = length(unit), k1 = length(slope);
        if (k1 == 0.0) return -std::min(lump.radii[0], std::min(lump.radii[1], lump.radii[2]));
        // Far from a flat ball the first value grows too slowly; the
        // distance to the sphere around the ball is the floor under it.
        const double widest = std::max(lump.radii[0], std::max(lump.radii[1], lump.radii[2]));
        return std::max(k0 * (k0 - 1.0) / k1, length(q) - widest);
    }
    case LumpKind::Capsule: {
        // Two balls and the cone that touches both: in the plane of the
        // axis and the point, the nearest part is found along the cone's
        // side.
        const Point3 pa = sub(p, lump.a), ba = sub(lump.b, lump.a);
        const double span = length(ba), r0 = lump.radii[0], r1 = lump.radii[1];
        // One end holds the other: the larger ball alone.
        if (!(span > std::fabs(r0 - r1))) return r0 >= r1 ? length(pa) - r0 : length(sub(p, lump.b)) - r1;
        const double t = dot(pa, ba) / span;
        const double rho = std::sqrt(std::max(dot(pa, pa) - t * t, 0.0));
        const double s = (r0 - r1) / span, c = std::sqrt(1.0 - s * s);
        const double k = t * c - rho * s;
        if (k <= 0.0) return length(pa) - r0;
        if (k >= span * c) return length(sub(p, lump.b)) - r1;
        return (t * s + rho * c) - r0;
    }
    case LumpKind::Slab: {
        const Point3 d = sub(p, lump.a);
        const double q[3] = {std::fabs(dot(lump.axes[0], d)) - (lump.radii[0] - lump.round),
                             std::fabs(dot(lump.axes[1], d)) - (lump.radii[1] - lump.round),
                             std::fabs(dot(lump.axes[2], d)) - (lump.radii[2] - lump.round)};
        const Point3 outside{std::max(q[0], 0.0), std::max(q[1], 0.0), std::max(q[2], 0.0)};
        const double inside = std::min(std::max(q[0], std::max(q[1], q[2])), 0.0);
        return (length(outside) + inside) - lump.round;
    }
    }
    return std::numeric_limits<double>::infinity();
}

// The smooth minimum: where the two values differ by less than `blend` the
// result dips below both, by a quarter of `blend` where they are equal.
double join(double a, double b, double blend) {
    double m = std::min(a, b);
    if (blend > 0.0) {
        const double h = std::max(blend - std::fabs(a - b), 0.0);
        m = m - h * h / (4.0 * blend);
    }
    return m;
}

// How far a lump reaches from its centre along any axis of the block.
double reach(const Lump& lump) {
    switch (lump.kind) {
    case LumpKind::Ball: return std::max(lump.radii[0], std::max(lump.radii[1], lump.radii[2]));
    case LumpKind::Capsule: return std::max(lump.radii[0], lump.radii[1]);
    case LumpKind::Slab: return length({lump.radii[0], lump.radii[1], lump.radii[2]});
    }
    return 0.0;
}

std::size_t nodeIndex(const SculptGrid& grid, u32 i, u32 j, u32 k) {
    return (static_cast<std::size_t>(k) * grid.nodes[1] + j) * grid.nodes[0] + i;
}

Point3 normalizedOr(const Point3& v, const Point3& fallback) {
    const double size = length(v);
    if (!(size > 0.0) || !std::isfinite(size)) return fallback;
    return {v.x / size, v.y / size, v.z / size};
}

// The frame of a projection: the unit direction of rising u, the unit
// direction up the image, both seen from outside along the projection's
// axis, so that their cross product is the side the triangle faces.
struct Plane {
    Point3 right, up;
};
constexpr Plane kPlanes[6] = {
    {{0, 0, -1}, {0, 1, 0}},  // facing +X
    {{0, 0, 1}, {0, 1, 0}},   // facing -X
    {{1, 0, 0}, {0, 0, -1}},  // facing +Y
    {{1, 0, 0}, {0, 0, 1}},   // facing -Y
    {{1, 0, 0}, {0, 1, 0}},   // facing +Z
    {{-1, 0, 0}, {0, 1, 0}},  // facing -Z
};

} // namespace

double sculptField(std::span<const Lump> lumps, double blend, const Point3& p) {
    double field = std::numeric_limits<double>::infinity();
    for (const Lump& lump : lumps) {
        const double distance = lumpDistance(lump, p);
        // Cutting b out of a is the larger of a and -b, smoothed the same way.
        field = lump.carve ? -join(-field, distance, blend) : join(field, distance, blend);
    }
    return field;
}

SculptGrid sculptGrid(std::span<const Lump> lumps, double cell, double blend) {
    SculptGrid grid;
    bool any = false;
    double low[3] = {0, 0, 0}, high[3] = {0, 0, 0};
    for (const Lump& lump : lumps) {
        if (lump.carve) continue;
        const double r = reach(lump);
        const Point3 ends[2] = {lump.a, lump.kind == LumpKind::Capsule ? lump.b : lump.a};
        for (const Point3& end : ends) {
            const double p[3] = {end.x, end.y, end.z};
            for (int axis = 0; axis < 3; ++axis) {
                if (!any || p[axis] - r < low[axis]) low[axis] = p[axis] - r;
                if (!any || p[axis] + r > high[axis]) high[axis] = p[axis] + r;
            }
            any = true;
        }
    }
    if (!any) return grid;
    for (int axis = 0; axis < 3; ++axis) {
        // A join bulges by a quarter of the blend at most; one whole cell
        // of margin on each side keeps the outermost nodes outside.
        const double first = std::floor((low[axis] - blend) / cell) - 1.0;
        const double last = std::ceil((high[axis] + blend) / cell) + 1.0;
        if (!std::isfinite(first) || !std::isfinite(last) || std::fabs(first) > 1.0e9 || std::fabs(last) > 1.0e9)
            violate("sculpt lumps are out of range for their cell");
        if (last - first > static_cast<double>(kMaxSculptCells))
            violate("sculpt grid exceeds 128 cells on an axis", ErrorReason::RuntimeViolation);
        grid.origin[axis] = static_cast<i64>(first);
        grid.nodes[axis] = static_cast<u32>(last - first) + 1u;
    }
    return grid;
}

std::vector<double> sculptSamples(std::span<const Lump> lumps, double blend, double cell, const SculptGrid& grid) {
    std::vector<double> samples(static_cast<std::size_t>(grid.samples()));
    std::size_t at = 0;
    for (u32 k = 0; k < grid.nodes[2]; ++k)
        for (u32 j = 0; j < grid.nodes[1]; ++j)
            for (u32 i = 0; i < grid.nodes[0]; ++i) {
                const Point3 p{static_cast<double>(grid.origin[0] + i) * cell, static_cast<double>(grid.origin[1] + j) * cell,
                               static_cast<double>(grid.origin[2] + k) * cell};
                samples[at++] = sculptField(lumps, blend, p);
            }
    return samples;
}

SculptCounts sculptCount(const SculptGrid& grid, std::span<const double> samples) {
    SculptCounts counts;
    const u32 nx = grid.nodes[0], ny = grid.nodes[1], nz = grid.nodes[2];
    if (nx < 2 || ny < 2 || nz < 2) return counts;
    const u32 cx = nx - 1, cy = ny - 1, cz = nz - 1;
    const auto inside = [&](u32 i, u32 j, u32 k) { return samples[nodeIndex(grid, i, j, k)] < 0.0; };
    for (u32 k = 0; k < cz; ++k)
        for (u32 j = 0; j < cy; ++j)
            for (u32 i = 0; i < cx; ++i) {
                unsigned in = 0;
                for (u32 c = 0; c < 8; ++c) in += inside(i + (c & 1u), j + ((c >> 1) & 1u), k + (c >> 2));
                if (in != 0 && in != 8) ++counts.points;
            }
    // The edges that have four cells around them, as the net visits them.
    for (u32 k = 1; k < cz; ++k)
        for (u32 j = 1; j < cy; ++j)
            for (u32 i = 0; i < cx; ++i) counts.triangles += inside(i, j, k) != inside(i + 1, j, k) ? 2u : 0u;
    for (u32 k = 1; k < cz; ++k)
        for (u32 j = 0; j < cy; ++j)
            for (u32 i = 1; i < cx; ++i) counts.triangles += inside(i, j, k) != inside(i, j + 1, k) ? 2u : 0u;
    for (u32 k = 0; k < cz; ++k)
        for (u32 j = 1; j < cy; ++j)
            for (u32 i = 1; i < cx; ++i) counts.triangles += inside(i, j, k) != inside(i, j, k + 1) ? 2u : 0u;
    return counts;
}

SculptNet sculptNet(const SculptGrid& grid, std::span<const double> samples, double cell) {
    SculptNet net;
    const u32 nx = grid.nodes[0], ny = grid.nodes[1], nz = grid.nodes[2];
    if (nx < 2 || ny < 2 || nz < 2) return net;
    const u32 cx = nx - 1, cy = ny - 1, cz = nz - 1;
    constexpr u32 kNone = 0xFFFFFFFFu;
    std::vector<u32> cellPoint(static_cast<std::size_t>(cx) * cy * cz, kNone);
    const auto cellIndex = [&](u32 i, u32 j, u32 k) { return (static_cast<std::size_t>(k) * cy + j) * cx + i; };
    const auto value = [&](u32 i, u32 j, u32 k) { return samples[nodeIndex(grid, i, j, k)]; };
    // Inside is strictly negative: a node exactly on the surface is outside.
    const auto inside = [&](u32 i, u32 j, u32 k) { return value(i, j, k) < 0.0; };

    // One point for each cell with corners on both sides: the mean of the
    // crossings of its twelve edges, each found by a straight line between
    // the two samples.
    static constexpr u8 corners[8][3] = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {1, 1, 0}, {0, 0, 1}, {1, 0, 1}, {0, 1, 1}, {1, 1, 1}};
    static constexpr u8 edges[12][2] = {{0, 1}, {2, 3}, {4, 5}, {6, 7}, {0, 2}, {1, 3}, {4, 6}, {5, 7}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
    for (u32 k = 0; k < cz; ++k)
        for (u32 j = 0; j < cy; ++j)
            for (u32 i = 0; i < cx; ++i) {
                double f[8];
                unsigned in = 0;
                for (int c = 0; c < 8; ++c) {
                    f[c] = value(i + corners[c][0], j + corners[c][1], k + corners[c][2]);
                    in += f[c] < 0.0;
                }
                if (in == 0 || in == 8) continue;
                double sum[3] = {0, 0, 0};
                unsigned crossings = 0;
                for (const auto& edge : edges) {
                    const double a = f[edge[0]], b = f[edge[1]];
                    if ((a < 0.0) == (b < 0.0)) continue;
                    const double t = a / (a - b);
                    for (int axis = 0; axis < 3; ++axis) {
                        const double from = corners[edge[0]][axis], to = corners[edge[1]][axis];
                        sum[axis] = sum[axis] + (from + (to - from) * t);
                    }
                    ++crossings;
                }
                const double n = static_cast<double>(crossings);
                cellPoint[cellIndex(i, j, k)] = static_cast<u32>(net.points.size());
                net.points.push_back({(static_cast<double>(grid.origin[0] + i) + sum[0] / n) * cell,
                                      (static_cast<double>(grid.origin[1] + j) + sum[1] / n) * cell,
                                      (static_cast<double>(grid.origin[2] + k) + sum[2] / n) * cell});
            }

    // Two triangles for each grid edge whose ends lie on different sides,
    // through the points of the four cells around it. They turn
    // counterclockwise about the edge's direction when the inside is at its
    // lower end, and the diagonal starts at the cell of the lowest index.
    const auto quad = [&](std::array<std::size_t, 4> cells, bool reversed) {
        if (reversed) std::swap(cells[1], cells[3]);
        std::size_t first = 0;
        for (std::size_t c = 1; c < 4; ++c)
            if (cells[c] < cells[first]) first = c;
        u32 p[4];
        for (std::size_t c = 0; c < 4; ++c) p[c] = cellPoint[cells[(first + c) % 4]];
        net.triangles.push_back({p[0], p[1], p[2]});
        net.triangles.push_back({p[0], p[2], p[3]});
    };
    for (u32 k = 1; k < cz; ++k)
        for (u32 j = 1; j < cy; ++j)
            for (u32 i = 0; i < cx; ++i) {
                const bool lower = inside(i, j, k);
                if (lower == inside(i + 1, j, k)) continue;
                quad({cellIndex(i, j - 1, k - 1), cellIndex(i, j, k - 1), cellIndex(i, j, k), cellIndex(i, j - 1, k)}, !lower);
            }
    for (u32 k = 1; k < cz; ++k)
        for (u32 j = 0; j < cy; ++j)
            for (u32 i = 1; i < cx; ++i) {
                const bool lower = inside(i, j, k);
                if (lower == inside(i, j + 1, k)) continue;
                quad({cellIndex(i - 1, j, k - 1), cellIndex(i - 1, j, k), cellIndex(i, j, k), cellIndex(i, j, k - 1)}, !lower);
            }
    for (u32 k = 0; k < cz; ++k)
        for (u32 j = 1; j < cy; ++j)
            for (u32 i = 1; i < cx; ++i) {
                const bool lower = inside(i, j, k);
                if (lower == inside(i, j, k + 1)) continue;
                quad({cellIndex(i - 1, j - 1, k), cellIndex(i, j - 1, k), cellIndex(i, j, k), cellIndex(i - 1, j, k)}, !lower);
            }

    // The plane each triangle is mapped by: the axis its face looks along
    // most, X before Y before Z when two are equal, and the side it faces.
    net.projection.reserve(net.triangles.size());
    std::vector<u8> used(net.points.size(), 0);
    for (const auto& triangle : net.triangles) {
        const Point3 n = cross(sub(net.points[triangle[1]], net.points[triangle[0]]),
                               sub(net.points[triangle[2]], net.points[triangle[0]]));
        const double size[3] = {std::fabs(n.x), std::fabs(n.y), std::fabs(n.z)};
        const double component[3] = {n.x, n.y, n.z};
        const int axis = size[0] >= size[1] && size[0] >= size[2] ? 0 : size[1] >= size[2] ? 1 : 2;
        const u8 projection = static_cast<u8>(axis * 2 + (component[axis] < 0.0 ? 1 : 0));
        net.projection.push_back(projection);
        for (const u32 point : triangle) {
            if (!(used[point] & (1u << projection))) ++net.vertices;
            used[point] = static_cast<u8>(used[point] | (1u << projection));
        }
    }
    return net;
}

void sculptMesh(LocalMesh& mesh, const SculptNet& net, std::span<const Lump> lumps, double blend, double cell,
                const Mapping& mapping, u32 skin) {
    // The normal of a point: the gradient of the field by central
    // differences a quarter of a cell wide. Where the field is flat, the sum
    // of the normals of the triangles at the point, and failing that, up.
    std::vector<Point3> faces(net.points.size());
    for (const auto& triangle : net.triangles) {
        const Point3 n = cross(sub(net.points[triangle[1]], net.points[triangle[0]]),
                               sub(net.points[triangle[2]], net.points[triangle[0]]));
        for (const u32 point : triangle) faces[point] = add(faces[point], n);
    }
    const double step = cell * 0.25;
    std::vector<Point3> normals(net.points.size());
    for (std::size_t i = 0; i < net.points.size(); ++i) {
        const Point3& p = net.points[i];
        const Point3 gradient{
            sculptField(lumps, blend, {p.x + step, p.y, p.z}) - sculptField(lumps, blend, {p.x - step, p.y, p.z}),
            sculptField(lumps, blend, {p.x, p.y + step, p.z}) - sculptField(lumps, blend, {p.x, p.y - step, p.z}),
            sculptField(lumps, blend, {p.x, p.y, p.z + step}) - sculptField(lumps, blend, {p.x, p.y, p.z - step})};
        normals[i] = normalizedOr(gradient, normalizedOr(faces[i], {0, 1, 0}));
    }

    // A point becomes a vertex once for each projection that uses it, in the
    // order the triangles first use them.
    constexpr u32 kNone = 0xFFFFFFFFu;
    std::vector<std::array<u32, 6>> vertexOf(net.points.size(), {kNone, kNone, kNone, kNone, kNone, kNone});
    const u32 first = static_cast<u32>(mesh.vertices.size());
    for (std::size_t t = 0; t < net.triangles.size(); ++t) {
        const u8 projection = net.projection[t];
        const Plane& plane = kPlanes[projection];
        u32 corner[3];
        for (int c = 0; c < 3; ++c) {
            const u32 point = net.triangles[t][static_cast<std::size_t>(c)];
            u32& vertex = vertexOf[point][projection];
            if (vertex == kNone) {
                vertex = static_cast<u32>(mesh.vertices.size()) - first;
                const Point3& p = net.points[point];
                const Point3& n = normals[point];
                LocalVertex out;
                out.position = p;
                out.normal = n;
                // The direction of rising u in the tangent plane; where the
                // normal lies along it, the one a quarter turn from up.
                const Point3 flat = sub(plane.right, scaled(n, dot(n, plane.right)));
                out.tangent = normalizedOr(flat, normalizedOr(cross(plane.up, n), plane.right));
                out.w = 1.0;
                const double s = dot(p, plane.right), down = -dot(p, plane.up);
                out.uv[0] = static_cast<f32>((s * mapping.density + mapping.shiftU) / mapping.assetWidth);
                out.uv[1] = static_cast<f32>((down * mapping.density + mapping.shiftV) / mapping.assetHeight);
                out.skin = skin;
                mesh.vertices.push_back(out);
            }
            corner[c] = first + vertex;
        }
        mesh.triangles.push_back({corner[0], corner[1], corner[2], skin});
    }
}

} // namespace iprop::model
