#include "iprop/model/Blocks.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <utility>

// Every expression below is written in the order the specification gives:
// one IEEE operation per operator, no contraction. Groups of vertices are
// visited in order of first appearance; maps only find them.

namespace iprop::model {
namespace {

constexpr double kPio2 = 1.57079632679489661923e+00;
constexpr double kRad = 0.017453292519943295;

[[noreturn]] void violate(const char* message, ErrorReason reason = ErrorReason::NumericRange) {
    throw Violation(message, reason);
}

Point3 add(const Point3& a, const Point3& b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
Point3 sub(const Point3& a, const Point3& b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
Point3 scaled(const Point3& v, double k) { return {v.x * k, v.y * k, v.z * k}; }
double dot(const Point3& a, const Point3& b) { return (a.x * b.x + a.y * b.y) + a.z * b.z; }
Point3 cross(const Point3& a, const Point3& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
bool zero(const Point3& v) { return v.x == 0.0 && v.y == 0.0 && v.z == 0.0; }
Point3 unit(const Point3& v) {
    const double length = std::sqrt((v.x * v.x + v.y * v.y) + v.z * v.z);
    if (!(length > 0.0) || !std::isfinite(length)) violate("degenerate direction", ErrorReason::RuntimeViolation);
    return {v.x / length, v.y / length, v.z / length};
}

// The tangent made perpendicular to a new unit normal; kept when it lies
// along the normal.
Point3 settled(const Point3& tangent, const Point3& normal) {
    const Point3 projected = sub(tangent, scaled(normal, dot(tangent, normal)));
    return zero(projected) ? tangent : unit(projected);
}

// turn(a).c of the specification: exact at multiples of 90 degrees.
double cosine(f32 degrees) {
    const double a = degrees;
    const double q = a / 90.0;
    if (q == std::floor(q)) {
        const double m = q - 4.0 * std::floor(q / 4.0);
        return m == 0.0 ? 1.0 : m == 2.0 ? -1.0 : 0.0;
    }
    return sin64(a * kRad + kPio2);
}

using Position = std::array<double, 3>;
Position key(const Point3& p) { return {p.x, p.y, p.z}; }

// Vertices of equal position, by value (-0 equals +0), each group listing
// its vertices in order and the groups in order of first appearance.
struct Groups {
    std::vector<u32> of;                  // the group of each vertex
    std::vector<std::vector<u32>> members;
};

Groups groupByPosition(const LocalMesh& mesh) {
    Groups groups;
    std::map<Position, u32> found;
    groups.of.reserve(mesh.vertices.size());
    for (u32 v = 0; v < mesh.vertices.size(); ++v) {
        const auto [entry, fresh] = found.emplace(key(mesh.vertices[v].position), static_cast<u32>(groups.members.size()));
        if (fresh) groups.members.emplace_back();
        groups.members[entry->second].push_back(v);
        groups.of.push_back(entry->second);
    }
    return groups;
}

// The cofactor matrix of a 3x3 matrix, each element a difference of two
// products in the order of the placement scopes.
void cofactor(const double a[3][3], double c[3][3]) {
    c[0][0] = a[1][1] * a[2][2] - a[1][2] * a[2][1];
    c[0][1] = a[1][2] * a[2][0] - a[1][0] * a[2][2];
    c[0][2] = a[1][0] * a[2][1] - a[1][1] * a[2][0];
    c[1][0] = a[0][2] * a[2][1] - a[0][1] * a[2][2];
    c[1][1] = a[0][0] * a[2][2] - a[0][2] * a[2][0];
    c[1][2] = a[0][1] * a[2][0] - a[0][0] * a[2][1];
    c[2][0] = a[0][1] * a[1][2] - a[0][2] * a[1][1];
    c[2][1] = a[0][2] * a[1][0] - a[0][0] * a[1][2];
    c[2][2] = a[0][0] * a[1][1] - a[0][1] * a[1][0];
}

Point3 apply(const double m[3][3], const Point3& v) {
    return {(m[0][0] * v.x + m[0][1] * v.y) + m[0][2] * v.z, (m[1][0] * v.x + m[1][1] * v.y) + m[1][2] * v.z,
            (m[2][0] * v.x + m[2][1] * v.y) + m[2][2] * v.z};
}

// A deformed point and the Jacobian there.
struct Mapped {
    Point3 point;
    double jacobian[3][3];
    double fold = 1.0; // a bend: how much of the arc's radius is left at the point
};

Mapped map(const Deformation& d, const Point3& p) {
    Mapped out;
    const double t = (p.y - d.y0) / d.length;
    switch (d.kind) {
    case DeformKind::Bend: {
        const double phi = t * d.a;
        const double s = sin64(phi), c = sin64(phi + kPio2);
        const double left = d.radius - p.x;
        const double k = left / d.radius;
        out.fold = k;
        out.point = {d.radius - left * c, d.y0 + left * s, p.z};
        const double j[3][3] = {{c, k * s, 0.0}, {-s, k * c, 0.0}, {0.0, 0.0, 1.0}};
        std::copy(&j[0][0], &j[0][0] + 9, &out.jacobian[0][0]);
        break;
    }
    case DeformKind::Twist: {
        const double theta = t * d.a;
        const double s = sin64(theta), c = sin64(theta + kPio2);
        const double kappa = d.a / d.length;
        const double x = c * p.x + s * p.z, z = c * p.z - s * p.x;
        out.point = {x, p.y, z};
        const double j[3][3] = {{c, kappa * z, s}, {0.0, 1.0, 0.0}, {-s, -(kappa * x), c}};
        std::copy(&j[0][0], &j[0][0] + 9, &out.jacobian[0][0]);
        break;
    }
    case DeformKind::Taper: {
        const double s = 1.0 + (d.a - 1.0) * t;
        const double slope = (d.a - 1.0) / d.length;
        out.point = {p.x * s, p.y, p.z * s};
        const double j[3][3] = {{s, p.x * slope, 0.0}, {0.0, 1.0, 0.0}, {0.0, p.z * slope, s}};
        std::copy(&j[0][0], &j[0][0] + 9, &out.jacobian[0][0]);
        break;
    }
    }
    return out;
}

// ---- noise in space ---------------------------------------------------------

u32 hash32(u32 x) {
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

double fade(double t) { return ((t * t) * t) * ((t * ((t * 6.0) - 15.0)) + 10.0); }

// The twelve edge directions of a cube, four of them twice (Perlin 2002).
constexpr double kGradients[16][3] = {
    {1, 1, 0}, {-1, 1, 0}, {1, -1, 0}, {-1, -1, 0}, {1, 0, 1}, {-1, 0, 1}, {1, 0, -1}, {-1, 0, -1},
    {0, 1, 1}, {0, -1, 1}, {0, 1, -1}, {0, -1, -1}, {1, 1, 0}, {0, -1, 1}, {-1, 1, 0}, {0, -1, -1}};

double lattice(double value) {
    const double cell = std::floor(value);
    if (!std::isfinite(value) || cell < -2147483648.0 || cell > 2147483646.0) violate("noise coordinate out of range");
    return cell;
}

} // namespace

double noise3(double x, double y, double z, u32 seed) {
    const double cx = lattice(x), cy = lattice(y), cz = lattice(z);
    const double fx = x - cx, fy = y - cy, fz = z - cz;
    const i32 ix = static_cast<i32>(cx), iy = static_cast<i32>(cy), iz = static_cast<i32>(cz);
    double n[2][2][2];
    for (int dz = 0; dz < 2; ++dz)
        for (int dy = 0; dy < 2; ++dy)
            for (int dx = 0; dx < 2; ++dx) {
                const u32 hx = static_cast<u32>(ix + dx), hy = static_cast<u32>(iy + dy), hz = static_cast<u32>(iz + dz);
                const u32 h = hash32(seed ^ (hx * 73856093u) ^ (hy * 19349663u) ^ (hz * 83492791u));
                const double* g = kGradients[h & 15u];
                n[dz][dy][dx] = (g[0] * (fx - dx) + g[1] * (fy - dy)) + g[2] * (fz - dz);
            }
    const double u = fade(fx), v = fade(fy), w = fade(fz);
    const double a00 = n[0][0][0] * (1.0 - u) + n[0][0][1] * u, a10 = n[0][1][0] * (1.0 - u) + n[0][1][1] * u;
    const double a01 = n[1][0][0] * (1.0 - u) + n[1][0][1] * u, a11 = n[1][1][0] * (1.0 - u) + n[1][1][1] * u;
    const double b0 = a00 * (1.0 - v) + a10 * v, b1 = a01 * (1.0 - v) + a11 * v;
    return b0 * (1.0 - w) + b1 * w;
}

void checkFinite(const LocalMesh& mesh) {
    for (const LocalVertex& vertex : mesh.vertices)
        if (!std::isfinite(vertex.position.x) || !std::isfinite(vertex.position.y) || !std::isfinite(vertex.position.z))
            violate("model coordinate out of range");
}

// ---- smooth -----------------------------------------------------------------

void smooth(LocalMesh& mesh, f32 degrees) {
    // An angle of 0 joins nothing.
    if (degrees == 0.f) return;
    const double limit = cosine(degrees);
    const Groups groups = groupByPosition(mesh);
    std::vector<Point3> normals(mesh.vertices.size());
    std::vector<bool> changed(mesh.vertices.size(), false);
    for (u32 v = 0; v < mesh.vertices.size(); ++v) {
        const Point3& own = mesh.vertices[v].normal;
        Point3 sum{0.0, 0.0, 0.0};
        bool joined = false;
        for (const u32 u : groups.members[groups.of[v]]) {
            const Point3& other = mesh.vertices[u].normal;
            if (u == v) {
                sum = add(sum, other);
            } else if (dot(other, own) > limit) {
                sum = add(sum, other);
                if (other.x != own.x || other.y != own.y || other.z != own.z) joined = true;
            }
        }
        // A vertex joined by no normal other than its own (a copy on a seam
        // of the image has the same one) keeps its normal and its tangent.
        changed[v] = joined && !zero(sum);
        if (changed[v]) normals[v] = unit(sum);
    }
    for (u32 v = 0; v < mesh.vertices.size(); ++v) {
        if (!changed[v]) continue;
        LocalVertex& vertex = mesh.vertices[v];
        vertex.normal = normals[v];
        vertex.tangent = settled(vertex.tangent, vertex.normal);
    }
}

// ---- refine -----------------------------------------------------------------

namespace {

using Edge = std::pair<u32, u32>;
Edge edgeOf(u32 a, u32 b) { return a < b ? Edge{a, b} : Edge{b, a}; }

// Whether the edge from p to q is walked from p: from the end of the
// smaller position, so that every copy of an edge, whatever its vertex
// numbers, gets the same points.
bool walksFrom(const LocalMesh& mesh, u32 p, u32 q) {
    const Position a = key(mesh.vertices[p].position), b = key(mesh.vertices[q].position);
    if (a != b) return a < b;
    return p < q;
}

LocalVertex blend(const LocalVertex& p, const LocalVertex& q, double f) {
    LocalVertex out;
    out.position = add(p.position, scaled(sub(q.position, p.position), f));
    out.normal = unit(add(p.normal, scaled(sub(q.normal, p.normal), f)));
    out.tangent = unit(add(p.tangent, scaled(sub(q.tangent, p.tangent), f)));
    out.w = p.w;
    out.uv[0] = static_cast<f32>(static_cast<double>(p.uv[0]) + (static_cast<double>(q.uv[0]) - p.uv[0]) * f);
    out.uv[1] = static_cast<f32>(static_cast<double>(p.uv[1]) + (static_cast<double>(q.uv[1]) - p.uv[1]) * f);
    out.skin = p.skin;
    return out;
}

} // namespace

Counts refineCounts(const LocalMesh& mesh, u32 n) {
    // refine(1) adds nothing: no need to count the edges.
    if (n <= 1) return {0, static_cast<u32>(mesh.triangles.size())};
    std::map<Edge, bool> edges;
    for (const Triangle& t : mesh.triangles) {
        edges.emplace(edgeOf(t.a, t.b), true);
        edges.emplace(edgeOf(t.b, t.c), true);
        edges.emplace(edgeOf(t.c, t.a), true);
    }
    const u64 inner = n >= 2 ? static_cast<u64>(n - 1) * (n - 2) / 2 : 0;
    const u64 added = static_cast<u64>(edges.size()) * (n - 1) + mesh.triangles.size() * inner;
    return {static_cast<u32>(added), static_cast<u32>(mesh.triangles.size() * static_cast<u64>(n) * n)};
}

void refine(LocalMesh& mesh, u32 n) {
    if (n <= 1) return;
    LocalMesh out;
    out.vertices = mesh.vertices;
    out.triangles.reserve(mesh.triangles.size() * static_cast<std::size_t>(n) * n);
    std::map<Edge, u32> edgeStart; // the first of the n - 1 points of an edge, walked from its start
    const auto edgePoint = [&](u32 x, u32 y, u32 k) -> u32 {
        if (k == 0) return x;
        if (k == n) return y;
        const Edge edge = edgeOf(x, y);
        const bool fromX = walksFrom(mesh, x, y);
        auto found = edgeStart.find(edge);
        if (found == edgeStart.end()) {
            const u32 start = fromX ? x : y, end = fromX ? y : x;
            found = edgeStart.emplace(edge, static_cast<u32>(out.vertices.size())).first;
            for (u32 m = 1; m < n; ++m)
                out.vertices.push_back(blend(mesh.vertices[start], mesh.vertices[end],
                                             static_cast<double>(m) / static_cast<double>(n)));
        }
        return found->second + (fromX ? k - 1 : n - k - 1);
    };
    std::vector<u32> grid;
    for (const Triangle& t : mesh.triangles) {
        const LocalVertex& a = mesh.vertices[t.a];
        const LocalVertex& b = mesh.vertices[t.b];
        const LocalVertex& c = mesh.vertices[t.c];
        // Lattice point (i, j) is a + (b - a) i/n + (c - a) j/n, rows j.
        grid.clear();
        const auto at = [&](u32 i, u32 j) { return grid[j * (n + 1) + i]; };
        grid.assign(static_cast<std::size_t>(n + 1) * (n + 1), 0);
        for (u32 j = 0; j <= n; ++j)
            for (u32 i = 0; i + j <= n; ++i) {
                u32 index;
                if (j == 0) index = edgePoint(t.a, t.b, i);
                else if (i == 0) index = edgePoint(t.a, t.c, j);
                else if (i + j == n) index = edgePoint(t.b, t.c, j);
                else {
                    const double wa = static_cast<double>(n - i - j) / n, wb = static_cast<double>(i) / n,
                                 wc = static_cast<double>(j) / n;
                    LocalVertex inner;
                    inner.position = add(add(scaled(a.position, wa), scaled(b.position, wb)), scaled(c.position, wc));
                    inner.normal = unit(add(add(scaled(a.normal, wa), scaled(b.normal, wb)), scaled(c.normal, wc)));
                    inner.tangent = unit(add(add(scaled(a.tangent, wa), scaled(b.tangent, wb)), scaled(c.tangent, wc)));
                    inner.w = a.w;
                    for (int axis = 0; axis < 2; ++axis)
                        inner.uv[axis] = static_cast<f32>((a.uv[axis] * wa + b.uv[axis] * wb) + c.uv[axis] * wc);
                    inner.skin = t.skin;
                    index = static_cast<u32>(out.vertices.size());
                    out.vertices.push_back(inner);
                }
                grid[j * (n + 1) + i] = index;
            }
        for (u32 j = 0; j < n; ++j)
            for (u32 i = 0; i + j < n; ++i) {
                out.triangles.push_back({at(i, j), at(i + 1, j), at(i, j + 1), t.skin});
                if (i + j + 1 < n) out.triangles.push_back({at(i + 1, j), at(i + 1, j + 1), at(i, j + 1), t.skin});
            }
    }
    mesh = std::move(out);
}

// ---- bend, twist and taper --------------------------------------------------

Deformation deformation(DeformKind kind, f32 parameter, const LocalMesh& mesh) {
    Deformation d;
    d.kind = kind;
    const bool neutral = kind == DeformKind::Taper ? parameter == 1.f : parameter == 0.f;
    if (neutral || mesh.vertices.empty()) return d;
    double low = mesh.vertices[0].position.y, high = low;
    for (const LocalVertex& vertex : mesh.vertices) {
        low = std::min(low, vertex.position.y);
        high = std::max(high, vertex.position.y);
    }
    const double length = high - low;
    if (!(length > 0.0)) return d;
    d.identity = false;
    d.y0 = low;
    d.length = length;
    if (kind == DeformKind::Taper) {
        d.a = parameter;
    } else {
        d.a = static_cast<double>(parameter) * kRad;
        if (kind == DeformKind::Bend) d.radius = length / d.a;
    }
    return d;
}

void deform(LocalMesh& mesh, const Deformation& d) {
    if (d.identity) return;
    for (LocalVertex& vertex : mesh.vertices) {
        const Mapped mapped = map(d, vertex.position);
        if (d.kind == DeformKind::Bend && !(mapped.fold > 0.0))
            violate("bend is too tight for its geometry", ErrorReason::RuntimeViolation);
        double c[3][3];
        cofactor(mapped.jacobian, c);
        vertex.position = mapped.point;
        vertex.normal = unit(apply(c, vertex.normal));
        vertex.tangent = unit(apply(mapped.jacobian, vertex.tangent));
    }
}

Point3 deformPoint(const Deformation& d, const Point3& p) {
    if (d.identity) return p;
    return map(d, p).point;
}

// ---- displace ---------------------------------------------------------------

void displace(LocalMesh& mesh, f32 amount, f32 frequency, u32 seed) {
    // An amount of 0 moves nothing, so it changes nothing.
    if (amount == 0.f) return;
    const Groups groups = groupByPosition(mesh);
    const double f = frequency, k = amount;
    // One move per position: copies of a vertex on a hard edge stay together.
    std::vector<Point3> moved(groups.members.size());
    for (std::size_t g = 0; g < groups.members.size(); ++g) {
        const Point3& p = mesh.vertices[groups.members[g][0]].position;
        Point3 sum{0.0, 0.0, 0.0};
        for (const u32 v : groups.members[g]) sum = add(sum, mesh.vertices[v].normal);
        if (zero(sum)) {
            moved[g] = p;
            continue;
        }
        const double offset = k * noise3(p.x * f, p.y * f, p.z * f, seed);
        moved[g] = add(p, scaled(unit(sum), offset));
    }
    // Normals from the moved triangles, summed per position and per normal
    // before the move: a seam of the image stays smooth, a hard edge hard.
    using Corner = std::array<double, 6>;
    const auto corner = [&](u32 v) {
        const LocalVertex& vertex = mesh.vertices[v];
        return Corner{vertex.position.x, vertex.position.y, vertex.position.z,
                      vertex.normal.x, vertex.normal.y, vertex.normal.z};
    };
    std::map<Corner, Point3> faces;
    for (const Triangle& t : mesh.triangles) {
        const Point3& a = moved[groups.of[t.a]];
        const Point3 face = cross(sub(moved[groups.of[t.b]], a), sub(moved[groups.of[t.c]], a));
        for (const u32 v : {t.a, t.b, t.c}) {
            auto [entry, fresh] = faces.emplace(corner(v), face);
            if (!fresh) entry->second = add(entry->second, face);
        }
    }
    std::vector<Point3> normals(mesh.vertices.size());
    for (u32 v = 0; v < mesh.vertices.size(); ++v) {
        const auto found = faces.find(corner(v));
        normals[v] = found == faces.end() || zero(found->second) ? mesh.vertices[v].normal : unit(found->second);
    }
    for (u32 v = 0; v < mesh.vertices.size(); ++v) {
        LocalVertex& vertex = mesh.vertices[v];
        vertex.position = moved[groups.of[v]];
        vertex.normal = normals[v];
        vertex.tangent = settled(vertex.tangent, vertex.normal);
    }
}

} // namespace iprop::model
