#include "iprop/model/Geometry.hpp"

#include "iprop/math/Float.hpp"
#include "iprop/model/Ops.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

// Every expression below is written in the order the specification gives:
// one IEEE operation per operator, no contraction (the build disables it).
// Do not regroup sums or factor common terms; the bits depend on it.

namespace iprop::model {
namespace {

constexpr double kPio2 = 1.57079632679489661923e+00;
constexpr double kPio2Hi = 1.57079632673412561417e+00; // 0x3FF921FB54400000
constexpr double kPio2Lo = 6.07710050650619224932e-11; // 0x3DD0B4611A626331
constexpr double kPi = 3.141592653589793;
constexpr double kTau = 6.283185307179586;
constexpr double kRad = 0.017453292519943295;
constexpr double kSqrt2 = 1.4142135623730951;

[[noreturn]] void violate(const char* message, ErrorReason reason = ErrorReason::NumericRange) {
    throw Violation(message, reason);
}

bool finite(f32 value) { return std::isfinite(value); }
bool finite(Vec3 value) { return finite(value.x) && finite(value.y) && finite(value.z); }
Point3 widen(Vec3 value) { return {value.x, value.y, value.z}; }

struct Turn {
    double c, s;
};

// turn(a) of the specification: exact at multiples of 90 degrees.
Turn turn(f32 degrees) {
    const double a = degrees;
    const double q = a / 90.0;
    if (q == std::floor(q)) {
        const double m = q - 4.0 * std::floor(q / 4.0);
        if (m == 0.0) return {1.0, 0.0};
        if (m == 1.0) return {0.0, 1.0};
        if (m == 2.0) return {-1.0, 0.0};
        return {0.0, -1.0};
    }
    const double r = a * kRad;
    return {sin64(r + kPio2), sin64(r)};
}

using Matrix = double[3][3];

void multiply(const Matrix a, const Matrix b, Matrix out) {
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            out[i][j] = (a[i][0] * b[0][j] + a[i][1] * b[1][j]) + a[i][2] * b[2][j];
}

Point3 unit(const Point3& v) {
    const double length = std::sqrt((v.x * v.x + v.y * v.y) + v.z * v.z);
    if (!(length > 0.0) || !std::isfinite(length)) violate("degenerate direction", ErrorReason::RuntimeViolation);
    return {v.x / length, v.y / length, v.z / length};
}

Point3 sub(const Point3& a, const Point3& b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
Point3 cross(const Point3& a, const Point3& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
double dot(const Point3& a, const Point3& b) { return (a.x * b.x + a.y * b.y) + a.z * b.z; }
double length(const Point3& v) { return std::sqrt((v.x * v.x + v.y * v.y) + v.z * v.z); }

// Appends vertices and triangles of one primitive to its target.
class Writer {
public:
    explicit Writer(const Target& target) : target_(target) {}

    u32 vertex(const Point3& p, const Point3& n, const Point3& t, double s, double tt, double fw, double fh,
               double w = 1.0) {
        const Affine& m = target_.scope;
        Vertex out{};
        const double coordinates[3] = {
            ((m.a[0][0] * p.x + m.a[0][1] * p.y) + m.a[0][2] * p.z) + m.o[0],
            ((m.a[1][0] * p.x + m.a[1][1] * p.y) + m.a[1][2] * p.z) + m.o[1],
            ((m.a[2][0] * p.x + m.a[2][1] * p.y) + m.a[2][2] * p.z) + m.o[2]};
        for (int i = 0; i < 3; ++i) {
            out.position[i] = static_cast<f32>(coordinates[i]);
            if (!std::isfinite(out.position[i])) violate("model coordinate out of range");
        }
        Point3 normal{(m.c[0][0] * n.x + m.c[0][1] * n.y) + m.c[0][2] * n.z,
                      (m.c[1][0] * n.x + m.c[1][1] * n.y) + m.c[1][2] * n.z,
                      (m.c[2][0] * n.x + m.c[2][1] * n.y) + m.c[2][2] * n.z};
        if (m.flipped) normal = {-normal.x, -normal.y, -normal.z};
        normal = unit(normal);
        const Point3 tangent = unit({(m.a[0][0] * t.x + m.a[0][1] * t.y) + m.a[0][2] * t.z,
                                     (m.a[1][0] * t.x + m.a[1][1] * t.y) + m.a[1][2] * t.z,
                                     (m.a[2][0] * t.x + m.a[2][1] * t.y) + m.a[2][2] * t.z});
        out.normal[0] = static_cast<f32>(normal.x);
        out.normal[1] = static_cast<f32>(normal.y);
        out.normal[2] = static_cast<f32>(normal.z);
        out.tangent[0] = static_cast<f32>(tangent.x);
        out.tangent[1] = static_cast<f32>(tangent.y);
        out.tangent[2] = static_cast<f32>(tangent.z);
        out.tangent[3] = static_cast<f32>(m.flipped ? -w : w);
        const Mapping& map = target_.mapping;
        if (map.fit) {
            out.uv[0] = static_cast<f32>(s / fw);
            out.uv[1] = static_cast<f32>(tt / fh);
        } else {
            out.uv[0] = static_cast<f32>((s * map.density + map.shiftU) / map.assetWidth);
            out.uv[1] = static_cast<f32>((tt * map.density + map.shiftV) / map.assetHeight);
        }
        const u32 index = static_cast<u32>(target_.mesh.vertices.size());
        target_.mesh.vertices.push_back(out);
        target_.mesh.vertexSkins.push_back(target_.skin);
        return index;
    }

    void triangle(u32 a, u32 b, u32 c) {
        if (target_.scope.flipped) std::swap(b, c);
        target_.mesh.triangles.push_back({a, b, c, target_.skin});
    }

    // Four vertices BL, BR, TR, TL already emitted from `first`.
    void quad(u32 first) {
        triangle(first, first + 1, first + 2);
        triangle(first, first + 2, first + 3);
    }

private:
    const Target& target_;
};

// The column directions around the axis: back, left, front, right.
std::vector<Point2> columns(u32 sides) {
    std::vector<Point2> out(sides + 1); // x and z of each direction
    for (u32 j = 0; j <= sides; ++j) {
        if ((4u * j) % sides == 0) {
            switch ((4u * j / sides) % 4u) {
            case 0: out[j] = {0.0, -1.0}; break;
            case 1: out[j] = {-1.0, 0.0}; break;
            case 2: out[j] = {0.0, 1.0}; break;
            default: out[j] = {1.0, 0.0}; break;
            }
        } else {
            const double a = (static_cast<double>(j) * kTau) / static_cast<double>(sides);
            out[j] = {-sin64(a), -sin64(a + kPio2)};
        }
    }
    return out;
}

void checkSides(i32 sides) {
    if (sides < static_cast<i32>(kMinSides) || sides > static_cast<i32>(kMaxSides)) violate("sides must be in 3..64");
}

// One row of a band: sides + 1 vertices of radius `r` at height `y` with the
// profile normal (nr, ny), at face coordinate `t`.
u32 row(Writer& out, const Point3& at, const std::vector<Point2>& dir, double r, double y, double nr, double ny,
        double circumference, double t, double fw, double fh) {
    const u32 sides = static_cast<u32>(dir.size() - 1);
    u32 first = 0;
    for (u32 j = 0; j <= sides; ++j) {
        const Point3 p{at.x + dir[j].x * r, at.y + y, at.z + dir[j].y * r};
        const Point3 n = unit({dir[j].x * nr, ny, dir[j].y * nr});
        const Point3 tangent{dir[j].y, 0.0, -dir[j].x};
        const double s = circumference * (static_cast<double>(j) / static_cast<double>(sides));
        const u32 index = out.vertex(p, n, tangent, s, t, fw, fh);
        if (j == 0) first = index;
    }
    return first;
}

// The triangles between two rows; a row of zero radius has collapsed.
void band(Writer& out, u32 lower, u32 upper, u32 sides, bool lowerOpen, bool upperOpen) {
    for (u32 j = 0; j < sides; ++j) {
        const u32 a = lower + j, b = lower + j + 1, c = upper + j + 1, d = upper + j;
        if (lowerOpen) out.triangle(a, b, c);
        if (upperOpen) out.triangle(a, c, d);
    }
}

// A flat disc: a centre vertex and one ring vertex per side.
void cap(Writer& out, const Point3& at, const std::vector<Point2>& dir, double r, double y, bool upper) {
    const u32 sides = static_cast<u32>(dir.size() - 1);
    const Point3 normal{0.0, upper ? 1.0 : -1.0, 0.0};
    const Point3 tangent{1.0, 0.0, 0.0};
    const double size = 2.0 * r;
    const u32 centre = out.vertex({at.x, at.y + y, at.z}, normal, tangent, r, r, size, size);
    for (u32 j = 0; j < sides; ++j) {
        const double lx = dir[j].x * r, lz = dir[j].y * r;
        out.vertex({at.x + lx, at.y + y, at.z + lz}, normal, tangent, lx + r, upper ? lz + r : r - lz, size, size);
    }
    for (u32 j = 0; j < sides; ++j) {
        const u32 here = centre + 1 + j, next = centre + 1 + (j + 1) % sides;
        if (upper) out.triangle(centre, here, next);
        else out.triangle(centre, next, here);
    }
}

double orient(const Point2& a, const Point2& b, const Point2& c) {
    return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
}

bool within(const Point2& p, const Point2& q, const Point2& r) {
    return std::min(p.x, q.x) <= r.x && r.x <= std::max(p.x, q.x) && std::min(p.y, q.y) <= r.y &&
           r.y <= std::max(p.y, q.y);
}

// Segment contact of the specification.
bool touch(const Point2& a, const Point2& b, const Point2& c, const Point2& d) {
    const double d1 = orient(a, b, c), d2 = orient(a, b, d), d3 = orient(c, d, a), d4 = orient(c, d, b);
    if (((d1 > 0.0 && d2 < 0.0) || (d1 < 0.0 && d2 > 0.0)) && ((d3 > 0.0 && d4 < 0.0) || (d3 < 0.0 && d4 > 0.0)))
        return true;
    return (d1 == 0.0 && within(a, b, c)) || (d2 == 0.0 && within(a, b, d)) || (d3 == 0.0 && within(c, d, a)) ||
           (d4 == 0.0 && within(c, d, b));
}

double twiceArea(const std::vector<Point2>& outline) {
    double sum = 0.0;
    const std::size_t n = outline.size();
    for (std::size_t i = 0; i < n; ++i) {
        const Point2& p = outline[i];
        const Point2& q = outline[(i + 1) % n];
        sum += p.x * q.y - q.x * p.y;
    }
    return sum;
}

// Steps 1 and 2 of extrude on an outline; false when a check fails. A
// negative area reverses the outline around its first point.
enum class OutlineFault { None, NoArea, Repeat, Crossing };
OutlineFault normalise(std::vector<Point2>& outline, bool allowReverse) {
    const double area = twiceArea(outline);
    if (!(area != 0.0) || !std::isfinite(area)) return OutlineFault::NoArea;
    if (area < 0.0) {
        if (!allowReverse) return OutlineFault::NoArea;
        std::reverse(outline.begin() + 1, outline.end());
    }
    const std::size_t n = outline.size();
    for (std::size_t i = 0; i < n; ++i) {
        const Point2& p = outline[i];
        const Point2& q = outline[(i + 1) % n];
        if (p.x == q.x && p.y == q.y) return OutlineFault::Repeat;
    }
    for (std::size_t i = 0; i < n; ++i) {
        for (std::size_t j = i + 1; j < n; ++j) {
            // Neighbours share a point.
            if (j == i + 1 || (i == 0 && j == n - 1)) continue;
            if (touch(outline[i], outline[(i + 1) % n], outline[j], outline[(j + 1) % n])) return OutlineFault::Crossing;
        }
    }
    return OutlineFault::None;
}

// The triangulation of the specification; empty when no ear is found.
std::vector<u32> triangulate(const std::vector<Point2>& outline) {
    std::vector<u32> remaining(outline.size());
    for (std::size_t i = 0; i < remaining.size(); ++i) remaining[i] = static_cast<u32>(i);
    std::vector<u32> triangles;
    triangles.reserve((outline.size() - 2) * 3);
    while (remaining.size() > 3) {
        const std::size_t n = remaining.size();
        bool found = false;
        for (std::size_t i = 0; i < n && !found; ++i) {
            const u32 u = remaining[(i + n - 1) % n], v = remaining[i], w = remaining[(i + 1) % n];
            if (!(orient(outline[u], outline[v], outline[w]) > 0.0)) continue;
            bool empty = true;
            for (std::size_t k = 0; k < n && empty; ++k) {
                const u32 x = remaining[k];
                if (x == u || x == v || x == w) continue;
                if (orient(outline[u], outline[v], outline[x]) >= 0.0 &&
                    orient(outline[v], outline[w], outline[x]) >= 0.0 &&
                    orient(outline[w], outline[u], outline[x]) >= 0.0)
                    empty = false;
            }
            if (!empty) continue;
            triangles.insert(triangles.end(), {u, v, w});
            remaining.erase(remaining.begin() + static_cast<std::ptrdiff_t>(i));
            found = true;
        }
        if (!found) return {};
    }
    triangles.insert(triangles.end(), {remaining[0], remaining[1], remaining[2]});
    return triangles;
}

// ---- the box tables ---------------------------------------------------------

struct Face {
    u8 corner[4][3]; // BL, BR, TR, TL as (x, y, z) picks of low (0) or high (1)
    int normal[3];
    int tangent[3];
    u8 normalAxis;
    u8 widthAxis, heightAxis; // which of w, h, d is fw and fh
};

constexpr Face kFaces[6] = {
    {{{0, 0, 1}, {1, 0, 1}, {1, 1, 1}, {0, 1, 1}}, {0, 0, 1}, {1, 0, 0}, 2, 0, 1},   // front
    {{{1, 0, 1}, {1, 0, 0}, {1, 1, 0}, {1, 1, 1}}, {1, 0, 0}, {0, 0, -1}, 0, 2, 1},  // right
    {{{1, 0, 0}, {0, 0, 0}, {0, 1, 0}, {1, 1, 0}}, {0, 0, -1}, {-1, 0, 0}, 2, 0, 1}, // back
    {{{0, 0, 0}, {0, 0, 1}, {0, 1, 1}, {0, 1, 0}}, {-1, 0, 0}, {0, 0, 1}, 0, 2, 1},  // left
    {{{0, 1, 1}, {1, 1, 1}, {1, 1, 0}, {0, 1, 0}}, {0, 1, 0}, {1, 0, 0}, 1, 0, 2},   // top
    {{{0, 0, 0}, {1, 0, 0}, {1, 0, 1}, {0, 0, 1}}, {0, -1, 0}, {1, 0, 0}, 1, 0, 2},  // bottom
};

enum Edge : u8 { Right, Left, Top, Bottom };
struct Strip {
    u8 p, edge, q;
};
constexpr Strip kStrips[12] = {
    {0, Right, 1}, {0, Left, 3}, {0, Top, 4}, {0, Bottom, 5}, {1, Right, 2}, {1, Top, 4},
    {1, Bottom, 5}, {2, Right, 3}, {2, Top, 4}, {2, Bottom, 5}, {3, Top, 4}, {3, Bottom, 5},
};

// Top corners at front-left, front-right, back-right, back-left, then the
// bottom ones, as (x, y, z) picks.
constexpr u8 kCorners[8][3] = {{0, 1, 1}, {1, 1, 1}, {1, 1, 0}, {0, 1, 0}, {0, 0, 1}, {1, 0, 1}, {1, 0, 0}, {0, 0, 0}};

struct BoxFrame {
    double x[4], y[4], z[4]; // low, low + b, high - b, high
    double size[3];          // w, h, d
    double b, e;

    // A corner of a face: on the face's own axis its plane, on the other
    // two the inset coordinate.
    Point3 corner(const Face& face, const u8 pick[3]) const {
        const auto index = [&](int axis) -> int {
            if (axis == face.normalAxis) return pick[axis] ? 3 : 0;
            return pick[axis] ? 2 : 1;
        };
        return {x[index(0)], y[index(1)], z[index(2)]};
    }
    double width(const Face& face) const { return size[face.widthAxis]; }
    double height(const Face& face) const { return size[face.heightAxis]; }
    // The face coordinates of corner slot 0..3 (BL, BR, TR, TL).
    Point2 coordinates(const Face& face, int slot) const {
        const double fw = width(face), fh = height(face);
        const double s = slot == 0 || slot == 3 ? b : fw - b;
        const double t = slot == 0 || slot == 1 ? fh - b : b;
        return {s, t};
    }
};

Point3 axis(const int v[3]) { return {static_cast<double>(v[0]), static_cast<double>(v[1]), static_cast<double>(v[2])}; }

int slotOf(const Face& face, const u8 pick[3]) {
    for (int slot = 0; slot < 4; ++slot)
        if (face.corner[slot][0] == pick[0] && face.corner[slot][1] == pick[1] && face.corner[slot][2] == pick[2])
            return slot;
    return 0; // unreachable: every corner of the tables lies on its faces
}

} // namespace

// Spec models v1.2, Conventions: the pinned sine of iprop-v0.8 §12.7 on a
// binary64 argument, without its final cast.
double sin64(double x) {
    const double k = std::floor(x / kPio2 + 0.5);
    const double r = (x - k * kPio2Hi) - k * kPio2Lo;
    const double r2 = r * r;
    const double s =
        r * (1.0 + r2 * (-1.0 / 6.0 +
                         r2 * (1.0 / 120.0 +
                               r2 * (-1.0 / 5040.0 + r2 * (1.0 / 362880.0 + r2 * (-1.0 / 39916800.0))))));
    const double c =
        1.0 + r2 * (-0.5 +
                    r2 * (1.0 / 24.0 +
                          r2 * (-1.0 / 720.0 +
                                r2 * (1.0 / 40320.0 + r2 * (-1.0 / 3628800.0 + r2 * (1.0 / 479001600.0))))));
    const double m = k - 4.0 * std::floor(k * 0.25);
    if (m == 0.0) return s;
    if (m == 1.0) return c;
    if (m == 2.0) return -s;
    return -c;
}

// ---- placement --------------------------------------------------------------

Affine place(const Affine& parent, const Placement& placement) {
    const u16 mask = placement.mask;
    if (!finite(placement.at) || !finite(placement.yaw) || !finite(placement.pitch) || !finite(placement.roll) ||
        !finite(placement.scale[0]) || !finite(placement.scale[1]) || !finite(placement.scale[2]))
        violate("place needs finite arguments");
    for (const f32 component : placement.scale)
        if (!(component > 0.f)) violate("place scale must be positive");
    for (const i32 mirror : placement.mirror)
        if (mirror != 0 && mirror != 1) violate("place mirror must be 0 or 1");

    const Turn yaw = turn(mask & kPlaceYaw ? placement.yaw : 0.f);
    const Turn pitch = turn(mask & kPlacePitch ? placement.pitch : 0.f);
    const Turn roll = turn(mask & kPlaceRoll ? placement.roll : 0.f);
    const Matrix ry = {{yaw.c, 0.0, yaw.s}, {0.0, 1.0, 0.0}, {-yaw.s, 0.0, yaw.c}};
    const Matrix rx = {{1.0, 0.0, 0.0}, {0.0, pitch.c, -pitch.s}, {0.0, pitch.s, pitch.c}};
    const Matrix rz = {{roll.c, -roll.s, 0.0}, {roll.s, roll.c, 0.0}, {0.0, 0.0, 1.0}};
    Matrix ryx, r, local;
    multiply(ry, rx, ryx);
    multiply(ryx, rz, r);
    double k[3];
    bool odd = false;
    for (int j = 0; j < 3; ++j) {
        const double m = placement.mirror[j] ? -1.0 : 1.0;
        k[j] = static_cast<double>(placement.scale[j]) * m;
        odd = odd != (placement.mirror[j] != 0);
    }
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) local[i][j] = r[i][j] * k[j];

    Affine child;
    multiply(parent.a, local, child.a);
    const Point3 at = widen(mask & kPlaceAt ? placement.at : Vec3{});
    for (int i = 0; i < 3; ++i)
        child.o[i] = ((parent.a[i][0] * at.x + parent.a[i][1] * at.y) + parent.a[i][2] * at.z) + parent.o[i];
    child.flipped = parent.flipped != odd;

    const auto& a = child.a;
    child.c[0][0] = a[1][1] * a[2][2] - a[1][2] * a[2][1];
    child.c[0][1] = a[1][2] * a[2][0] - a[1][0] * a[2][2];
    child.c[0][2] = a[1][0] * a[2][1] - a[1][1] * a[2][0];
    child.c[1][0] = a[0][2] * a[2][1] - a[0][1] * a[2][2];
    child.c[1][1] = a[0][0] * a[2][2] - a[0][2] * a[2][0];
    child.c[1][2] = a[0][1] * a[2][0] - a[0][0] * a[2][1];
    child.c[2][0] = a[0][1] * a[1][2] - a[0][2] * a[1][1];
    child.c[2][1] = a[0][2] * a[1][0] - a[0][0] * a[1][2];
    child.c[2][2] = a[0][0] * a[1][1] - a[0][1] * a[1][0];
    return child;
}

// ---- box --------------------------------------------------------------------

BoxShape checkBox(Vec3 at, Vec3 size) {
    if (!finite(at) || !finite(size)) violate("box needs finite arguments");
    if (!(size.x > 0.f) || !(size.y > 0.f) || !(size.z > 0.f)) violate("box size must be positive");
    return {widen(at), size.x, size.y, size.z, 0.0};
}

BoxShape checkBox(Vec3 at, Vec3 size, f32 bevel) {
    BoxShape shape = checkBox(at, size);
    const double shortest = std::min({shape.w, shape.h, shape.d});
    if (!finite(bevel) || !(bevel > 0.f) || !(2.0 * static_cast<double>(bevel) < shortest))
        violate("bevel must be positive and less than half the shortest side");
    shape.bevel = bevel;
    return shape;
}

Counts count(const BoxShape& shape) { return shape.bevel > 0.0 ? Counts{96, 44} : Counts{24, 12}; }

void emit(const Target& target, const BoxShape& shape) {
    Writer out(target);
    BoxFrame frame{};
    const double b = shape.bevel;
    const double x0 = shape.at.x - shape.w * 0.5, x1 = shape.at.x + shape.w * 0.5;
    const double y0 = shape.at.y, y1 = shape.at.y + shape.h;
    const double z0 = shape.at.z - shape.d * 0.5, z1 = shape.at.z + shape.d * 0.5;
    const auto fill = [b](double low, double high, double (&slots)[4]) {
        slots[0] = low;
        slots[1] = low + b;
        slots[2] = high - b;
        slots[3] = high;
    };
    fill(x0, x1, frame.x);
    fill(y0, y1, frame.y);
    fill(z0, z1, frame.z);
    frame.size[0] = shape.w;
    frame.size[1] = shape.h;
    frame.size[2] = shape.d;
    frame.b = b;
    frame.e = b * kSqrt2;

    // The six faces: with a bevel every corner sits inset on its plane.
    for (const Face& face : kFaces) {
        const double fw = frame.width(face), fh = frame.height(face);
        u32 first = 0;
        for (int slot = 0; slot < 4; ++slot) {
            const Point2 st = frame.coordinates(face, slot);
            const u32 index = out.vertex(frame.corner(face, face.corner[slot]), axis(face.normal), axis(face.tangent),
                                         st.x, st.y, fw, fh);
            if (slot == 0) first = index;
        }
        out.quad(first);
    }
    if (!(b > 0.0)) return;

    const double e = frame.e;
    for (const Strip& strip : kStrips) {
        const Face& p = kFaces[strip.p];
        const Face& q = kFaces[strip.q];
        const double fw = frame.width(p), fh = frame.height(p);
        const Point3 pn = axis(p.normal), pt = axis(p.tangent), qn = axis(q.normal);
        const bool vertical = strip.edge == Right || strip.edge == Left;
        const Point3 qt = vertical ? axis(q.tangent) : pt;
        const auto own = [&](int slot, double s, double t) {
            return out.vertex(frame.corner(p, p.corner[slot]), pn, pt, s, t, fw, fh);
        };
        const auto other = [&](int slot, double s, double t) {
            return out.vertex(frame.corner(q, p.corner[slot]), qn, qt, s, t, fw, fh);
        };
        u32 first = 0;
        switch (strip.edge) {
        case Right:
            first = own(1, fw - b, fh - b);
            other(1, fw - b + e, fh - b);
            other(2, fw - b + e, b);
            own(2, fw - b, b);
            break;
        case Left:
            first = other(0, b - e, fh - b);
            own(0, b, fh - b);
            own(3, b, b);
            other(3, b - e, b);
            break;
        case Top:
            first = own(3, b, b);
            own(2, fw - b, b);
            other(2, fw - b, b - e);
            other(3, b, b - e);
            break;
        default:
            first = other(0, b, fh - b + e);
            other(1, fw - b, fh - b + e);
            own(1, fw - b, fh - b);
            own(0, b, fh - b);
            break;
        }
        out.quad(first);
    }

    for (const auto& pick : kCorners) {
        const u8 zFace = pick[2] ? 0 : 2, xFace = pick[0] ? 1 : 3, yFace = pick[1] ? 4 : 5;
        const u8 firstFace = std::min(zFace, xFace), secondFace = std::max(zFace, xFace);
        const Face& a = kFaces[firstFace];
        const Face& s = kFaces[secondFace];
        const Face& y = kFaces[yFace];
        const int slot = slotOf(a, pick);
        const double fw = frame.width(a), fh = frame.height(a);
        const Point2 st = frame.coordinates(a, slot);
        const bool right = slot == 1 || slot == 2;
        const bool top = pick[1] != 0;
        // Counterclockwise from outside, decided on the signs alone: each
        // inset corner is the box corner moved by -b along the two axes of
        // its face, so the offsets are these integer vectors.
        const int d[3] = {pick[0] ? 1 : -1, pick[1] ? 1 : -1, pick[2] ? 1 : -1};
        const auto offset = [&](const Face& face, int out3[3]) {
            for (int k = 0; k < 3; ++k) out3[k] = k == face.normalAxis ? 0 : -d[k];
        };
        int va[3], vs[3], vy[3];
        offset(a, va);
        offset(s, vs);
        offset(y, vy);
        const int e1[3] = {vs[0] - va[0], vs[1] - va[1], vs[2] - va[2]};
        const int e2[3] = {vy[0] - va[0], vy[1] - va[1], vy[2] - va[2]};
        const int outward = (e1[1] * e2[2] - e1[2] * e2[1]) * d[0] + (e1[2] * e2[0] - e1[0] * e2[2]) * d[1] +
                            (e1[0] * e2[1] - e1[1] * e2[0]) * d[2];
        const auto side = [&] {
            return out.vertex(frame.corner(s, pick), axis(s.normal), axis(s.tangent), right ? st.x + e : st.x - e, st.y,
                              fw, fh);
        };
        const auto cover = [&] {
            return out.vertex(frame.corner(y, pick), axis(y.normal), axis(a.tangent), st.x, top ? st.y - e : st.y + e,
                              fw, fh);
        };
        const u32 first = out.vertex(frame.corner(a, pick), axis(a.normal), axis(a.tangent), st.x, st.y, fw, fh);
        if (outward > 0) {
            side();
            cover();
        } else {
            cover();
            side();
        }
        out.triangle(first, first + 1, first + 2);
    }
}

// ---- cone, cylinder, band ---------------------------------------------------

ConeShape checkCone(Vec3 at, f32 r0, f32 r1, f32 height, i32 sides, bool caps) {
    if (!finite(at) || !finite(r0) || !finite(r1) || !finite(height)) violate("cone needs finite arguments");
    if (r0 < 0.f || r1 < 0.f || !(std::max(r0, r1) > 0.f)) violate("radius must be positive");
    if (!(height > 0.f)) violate("height must be positive");
    checkSides(sides);
    return {widen(at), r0, r1, height, static_cast<u32>(sides), caps};
}

Counts count(const ConeShape& shape) {
    Counts counts;
    counts.vertices = 2 * (shape.sides + 1);
    counts.triangles = (shape.r0 > 0.0 ? shape.sides : 0) + (shape.r1 > 0.0 ? shape.sides : 0);
    if (shape.caps) {
        for (const double radius : {shape.r0, shape.r1}) {
            if (!(radius > 0.0)) continue;
            counts.vertices += shape.sides + 1;
            counts.triangles += shape.sides;
        }
    }
    return counts;
}

void emit(const Target& target, const ConeShape& shape) {
    Writer out(target);
    const auto dir = columns(shape.sides);
    const double biggest = std::max(shape.r0, shape.r1);
    const double circumference = kTau * biggest;
    const double slant =
        std::sqrt(shape.height * shape.height + (shape.r0 - shape.r1) * (shape.r0 - shape.r1));
    // The profile normal (nr, ny) of the side; row() normalises it per column.
    const double nr = shape.height, ny = shape.r0 - shape.r1;
    const u32 lower = row(out, shape.at, dir, shape.r0, 0.0, nr, ny, circumference, slant, circumference, slant);
    const u32 upper = row(out, shape.at, dir, shape.r1, shape.height, nr, ny, circumference, 0.0, circumference, slant);
    band(out, lower, upper, shape.sides, shape.r0 > 0.0, shape.r1 > 0.0);
    if (!shape.caps) return;
    if (shape.r0 > 0.0) cap(out, shape.at, dir, shape.r0, 0.0, false);
    if (shape.r1 > 0.0) cap(out, shape.at, dir, shape.r1, shape.height, true);
}

// ---- sphere -----------------------------------------------------------------

SphereShape checkSphere(Vec3 at, f32 radius, i32 sides, i32 rings) {
    if (!finite(at) || !finite(radius)) violate("sphere needs finite arguments");
    if (!(radius > 0.f)) violate("radius must be positive");
    checkSides(sides);
    if (rings < static_cast<i32>(kMinRings) || rings > static_cast<i32>(kMaxRings)) violate("rings must be in 2..32");
    return {widen(at), radius, static_cast<u32>(sides), static_cast<u32>(rings)};
}

Counts count(const SphereShape& shape) {
    return {(shape.sides + 1) * (shape.rings + 1), 2 * shape.sides * (shape.rings - 1)};
}

void emit(const Target& target, const SphereShape& shape) {
    Writer out(target);
    const auto dir = columns(shape.sides);
    const Point3 centre{shape.at.x, shape.at.y + shape.radius, shape.at.z};
    const double circumference = kTau * shape.radius, height = kPi * shape.radius;
    const u32 first = static_cast<u32>(target.mesh.vertices.size());
    for (u32 i = 0; i <= shape.rings; ++i) {
        double sp, cp;
        if (i == 0) {
            sp = 0.0;
            cp = 1.0;
        } else if (i == shape.rings) {
            sp = 0.0;
            cp = -1.0;
        } else if (2 * i == shape.rings) {
            sp = 1.0;
            cp = 0.0;
        } else {
            const double p = (static_cast<double>(i) * kPi) / static_cast<double>(shape.rings);
            sp = sin64(p);
            cp = sin64(p + kPio2);
        }
        const double t = height * (static_cast<double>(i) / static_cast<double>(shape.rings));
        for (u32 j = 0; j <= shape.sides; ++j) {
            const Point3 n{dir[j].x * sp, cp, dir[j].y * sp};
            const Point3 p{centre.x + n.x * shape.radius, centre.y + n.y * shape.radius, centre.z + n.z * shape.radius};
            const double s = circumference * (static_cast<double>(j) / static_cast<double>(shape.sides));
            out.vertex(p, n, {dir[j].y, 0.0, -dir[j].x}, s, t, circumference, height);
        }
    }
    const u32 stride = shape.sides + 1;
    for (u32 i = 0; i < shape.rings; ++i) {
        for (u32 j = 0; j < shape.sides; ++j) {
            const u32 a = first + (i + 1) * stride + j, b = a + 1;
            const u32 d = first + i * stride + j, c = d + 1;
            if (i != shape.rings - 1) out.triangle(a, b, c);
            if (i != 0) out.triangle(a, c, d);
        }
    }
}

// ---- lathe ------------------------------------------------------------------

LatheShape checkLathe(Vec3 at, std::span<const Vec2> profile, i32 sides, const f32* crease) {
    if (!finite(at)) violate("lathe needs finite arguments");
    if (profile.size() < 2 || profile.size() > kMaxProfilePoints) violate("lathe profile needs 2..64 points");
    checkSides(sides);
    f32 angle = 0.f;
    if (crease) {
        if (!finite(*crease) || *crease < 0.f || *crease > 180.f) violate("crease must be in 0..180");
        angle = *crease;
    }
    LatheShape shape;
    shape.at = widen(at);
    shape.sides = static_cast<u32>(sides);
    shape.profile.reserve(profile.size());
    for (const Vec2& point : profile) {
        if (!finite(point.x) || !finite(point.y)) violate("lathe needs finite arguments");
        if (point.x < 0.f) violate("lathe radius must not be negative");
        shape.profile.push_back({point.x, point.y});
        shape.maxRadius = std::max(shape.maxRadius, static_cast<double>(point.x));
    }

    const std::size_t count = shape.profile.size() - 1;
    std::vector<double> dr(count), dy(count), lengths(count);
    shape.cumulative.assign(shape.profile.size(), 0.0);
    for (std::size_t k = 0; k < count; ++k) {
        dr[k] = shape.profile[k + 1].x - shape.profile[k].x;
        dy[k] = shape.profile[k + 1].y - shape.profile[k].y;
        lengths[k] = std::sqrt(dr[k] * dr[k] + dy[k] * dy[k]);
        if (!(lengths[k] > 0.0)) violate("lathe profile repeats a point");
        if (!(shape.profile[k].x > 0.0) && !(shape.profile[k + 1].x > 0.0)) violate("lathe segment lies on the axis");
        shape.cumulative[k + 1] = shape.cumulative[k] + lengths[k];
    }

    // The profile normal of each segment, and of each smooth joint the
    // normalised sum of its two neighbours.
    struct Normal {
        double nr = 0, ny = 0;
    };
    std::vector<Normal> normals(count);
    for (std::size_t k = 0; k < count; ++k) normals[k] = {dy[k] / lengths[k], -dr[k] / lengths[k]};
    const double limit = turn(angle).c;
    std::vector<bool> smooth(count + 1, false);
    std::vector<Normal> blended(count + 1);
    for (std::size_t k = 1; angle > 0.f && k < count; ++k) {
        const double cosine = (dr[k - 1] * dr[k] + dy[k - 1] * dy[k]) / (lengths[k - 1] * lengths[k]);
        if (!(cosine > limit)) continue;
        const double sx = normals[k - 1].nr + normals[k].nr, sy = normals[k - 1].ny + normals[k].ny;
        const double sum = std::sqrt(sx * sx + sy * sy);
        if (!(sum > 0.0)) continue;
        smooth[k] = true;
        blended[k] = {sx / sum, sy / sum};
    }

    // Rows in emission order: a segment's lower row unless the joint below
    // it is smooth, then its upper row.
    for (std::size_t k = 0; k < count; ++k) {
        LatheShape::Segment segment;
        if (k > 0 && smooth[k]) segment.lower = shape.segments.back().upper;
        else {
            segment.lower = shape.rows.size();
            shape.rows.push_back({k, normals[k].nr, normals[k].ny});
        }
        segment.upper = shape.rows.size();
        if (k + 1 < count && smooth[k + 1]) shape.rows.push_back({k + 1, blended[k + 1].nr, blended[k + 1].ny});
        else shape.rows.push_back({k + 1, normals[k].nr, normals[k].ny});
        shape.segments.push_back(segment);
    }
    return shape;
}

Counts count(const LatheShape& shape) {
    Counts counts;
    counts.vertices = static_cast<u32>(shape.rows.size()) * (shape.sides + 1);
    for (const auto& segment : shape.segments) {
        if (shape.profile[shape.rows[segment.lower].point].x > 0.0) counts.triangles += shape.sides;
        if (shape.profile[shape.rows[segment.upper].point].x > 0.0) counts.triangles += shape.sides;
    }
    for (const Point2* end : {&shape.profile.front(), &shape.profile.back()}) {
        if (!(end->x > 0.0)) continue;
        counts.vertices += shape.sides + 1;
        counts.triangles += shape.sides;
    }
    return counts;
}

void emit(const Target& target, const LatheShape& shape) {
    Writer out(target);
    const auto dir = columns(shape.sides);
    const double circumference = kTau * shape.maxRadius, total = shape.cumulative.back();
    std::vector<u32> first(shape.rows.size());
    for (std::size_t r = 0; r < shape.rows.size(); ++r) {
        const auto& line = shape.rows[r];
        const Point2& point = shape.profile[line.point];
        first[r] = row(out, shape.at, dir, point.x, point.y, line.nr, line.ny, circumference,
                       total - shape.cumulative[line.point], circumference, total);
    }
    for (const auto& segment : shape.segments)
        band(out, first[segment.lower], first[segment.upper], shape.sides,
             shape.profile[shape.rows[segment.lower].point].x > 0.0,
             shape.profile[shape.rows[segment.upper].point].x > 0.0);
    if (shape.profile.front().x > 0.0) cap(out, shape.at, dir, shape.profile.front().x, shape.profile.front().y, false);
    if (shape.profile.back().x > 0.0) cap(out, shape.at, dir, shape.profile.back().x, shape.profile.back().y, true);
}

// ---- extrude ----------------------------------------------------------------

ExtrudeShape checkExtrude(Vec3 at, std::span<const Vec2> outline, f32 depth, const f32* bevel) {
    if (!finite(at) || !finite(depth)) violate("extrude needs finite arguments");
    if (outline.size() < 3 || outline.size() > kMaxOutlinePoints) violate("extrude outline needs 3..256 points");
    if (!(depth > 0.f)) violate("depth must be positive");
    ExtrudeShape shape;
    shape.at = widen(at);
    shape.depth = depth;
    shape.outline.reserve(outline.size());
    for (const Vec2& point : outline) {
        if (!finite(point.x) || !finite(point.y)) violate("extrude needs finite arguments");
        shape.outline.push_back({point.x, point.y});
    }
    switch (normalise(shape.outline, true)) {
    case OutlineFault::NoArea: violate("extrude outline has no area", ErrorReason::RuntimeViolation);
    case OutlineFault::Repeat: violate("extrude outline repeats a point");
    case OutlineFault::Crossing: violate("extrude outline is self-intersecting", ErrorReason::RuntimeViolation);
    case OutlineFault::None: break;
    }

    if (bevel) {
        if (!finite(*bevel) || !(*bevel > 0.f) || !(2.0 * static_cast<double>(*bevel) < shape.depth))
            violate("bevel must be positive and less than half the depth");
        shape.bevel = *bevel;
        const auto collapse = [] { violate("extrude bevel collapses the outline", ErrorReason::RuntimeViolation); };
        const std::size_t n = shape.outline.size();
        std::vector<Point2> normals(n);
        for (std::size_t i = 0; i < n; ++i) {
            const Point2& p = shape.outline[i];
            const Point2& q = shape.outline[(i + 1) % n];
            const double dx = q.x - p.x, dy = q.y - p.y;
            const double edge = std::sqrt(dx * dx + dy * dy);
            normals[i] = {dy / edge, -dx / edge};
        }
        shape.inset.resize(n);
        for (std::size_t i = 0; i < n; ++i) {
            const Point2& before = normals[(i + n - 1) % n];
            const Point2& after = normals[i];
            const double k = 1.0 + (before.x * after.x + before.y * after.y);
            if (!(k > 0.0)) collapse();
            shape.inset[i] = {shape.outline[i].x - shape.bevel * (before.x + after.x) / k,
                              shape.outline[i].y - shape.bevel * (before.y + after.y) / k};
        }
        for (std::size_t i = 0; i < n; ++i) {
            const std::size_t j = (i + 1) % n;
            const double along = (shape.inset[j].x - shape.inset[i].x) * (shape.outline[j].x - shape.outline[i].x) +
                                 (shape.inset[j].y - shape.inset[i].y) * (shape.outline[j].y - shape.outline[i].y);
            if (!(along > 0.0)) collapse();
        }
        auto probe = shape.inset;
        if (normalise(probe, false) != OutlineFault::None) collapse();
    }

    shape.triangles = triangulate(shape.bevel > 0.0 ? shape.inset : shape.outline);
    if (shape.triangles.empty()) violate("extrude outline cannot be triangulated", ErrorReason::RuntimeViolation);
    return shape;
}

Counts count(const ExtrudeShape& shape) {
    const u32 n = static_cast<u32>(shape.outline.size());
    const u32 capTriangles = static_cast<u32>(shape.triangles.size() / 3);
    if (shape.bevel > 0.0) return {2 * n + 4 * n + 8 * n, 2 * capTriangles + 2 * n + 4 * n};
    return {2 * n + 4 * n, 2 * capTriangles + 2 * n};
}

void emit(const Target& target, const ExtrudeShape& shape) {
    Writer out(target);
    const std::size_t n = shape.outline.size();
    const bool bevelled = shape.bevel > 0.0;
    const double b = shape.bevel, e = b * kSqrt2, depth = shape.depth;
    const auto& capLine = bevelled ? shape.inset : shape.outline;
    const double z1 = shape.at.z + depth * 0.5, z0 = shape.at.z - depth * 0.5;
    double minX = shape.outline[0].x, maxX = minX, minY = shape.outline[0].y, maxY = minY;
    for (const Point2& point : shape.outline) {
        minX = std::min(minX, point.x);
        maxX = std::max(maxX, point.x);
        minY = std::min(minY, point.y);
        maxY = std::max(maxY, point.y);
    }
    const double width = maxX - minX, height = maxY - minY;

    const u32 front = static_cast<u32>(target.mesh.vertices.size());
    for (const Point2& point : capLine)
        out.vertex({shape.at.x + point.x, shape.at.y + point.y, z1}, {0.0, 0.0, 1.0}, {1.0, 0.0, 0.0}, point.x - minX,
                   maxY - point.y, width, height);
    for (std::size_t i = 0; i < shape.triangles.size(); i += 3)
        out.triangle(front + shape.triangles[i], front + shape.triangles[i + 1], front + shape.triangles[i + 2]);

    const u32 back = static_cast<u32>(target.mesh.vertices.size());
    for (const Point2& point : capLine)
        out.vertex({shape.at.x + point.x, shape.at.y + point.y, z0}, {0.0, 0.0, -1.0}, {-1.0, 0.0, 0.0},
                   maxX - point.x, maxY - point.y, width, height);
    for (std::size_t i = 0; i < shape.triangles.size(); i += 3)
        out.triangle(back + shape.triangles[i], back + shape.triangles[i + 2], back + shape.triangles[i + 1]);

    // The perimeter up to each point; the last entry is the whole perimeter.
    std::vector<double> along(n + 1, 0.0);
    std::vector<Point2> normals(n), tangents(n);
    for (std::size_t i = 0; i < n; ++i) {
        const Point2& p = shape.outline[i];
        const Point2& q = shape.outline[(i + 1) % n];
        const double dx = q.x - p.x, dy = q.y - p.y;
        const double edge = std::sqrt(dx * dx + dy * dy);
        normals[i] = {dy / edge, -dx / edge};
        tangents[i] = {dx / edge, dy / edge};
        along[i + 1] = along[i] + edge;
    }
    const double total = along[n];
    const double zLow = bevelled ? z0 + b : z0, zHigh = bevelled ? z1 - b : z1;
    const double tLow = bevelled ? depth - b : depth, tHigh = bevelled ? b : 0.0;
    const auto at = [&](const Point2& point, double z) -> Point3 {
        return {shape.at.x + point.x, shape.at.y + point.y, z};
    };
    for (std::size_t i = 0; i < n; ++i) {
        const Point2& p = shape.outline[i];
        const Point2& q = shape.outline[(i + 1) % n];
        const Point3 normal{normals[i].x, normals[i].y, 0.0}, tangent{tangents[i].x, tangents[i].y, 0.0};
        const u32 first = out.vertex(at(p, zLow), normal, tangent, along[i], tLow, total, depth);
        out.vertex(at(q, zLow), normal, tangent, along[i + 1], tLow, total, depth);
        out.vertex(at(q, zHigh), normal, tangent, along[i + 1], tHigh, total, depth);
        out.vertex(at(p, zHigh), normal, tangent, along[i], tHigh, total, depth);
        out.quad(first);
    }
    if (!bevelled) return;

    for (std::size_t i = 0; i < n; ++i) {
        const std::size_t j = (i + 1) % n;
        const Point2& p = shape.outline[i];
        const Point2& q = shape.outline[j];
        const Point3 side{normals[i].x, normals[i].y, 0.0}, tangent{tangents[i].x, tangents[i].y, 0.0};
        const Point3 forward{0.0, 0.0, 1.0}, backward{0.0, 0.0, -1.0};
        u32 first = out.vertex(at(p, z1 - b), side, tangent, along[i], b, total, depth);
        out.vertex(at(q, z1 - b), side, tangent, along[i + 1], b, total, depth);
        out.vertex(at(shape.inset[j], z1), forward, tangent, along[i + 1], b - e, total, depth);
        out.vertex(at(shape.inset[i], z1), forward, tangent, along[i], b - e, total, depth);
        out.quad(first);
        first = out.vertex(at(shape.inset[i], z0), backward, tangent, along[i], depth - b + e, total, depth);
        out.vertex(at(shape.inset[j], z0), backward, tangent, along[i + 1], depth - b + e, total, depth);
        out.vertex(at(q, z0 + b), side, tangent, along[i + 1], depth - b, total, depth);
        out.vertex(at(p, z0 + b), side, tangent, along[i], depth - b, total, depth);
        out.quad(first);
    }
}

// ---- quad and tri -----------------------------------------------------------

QuadShape checkQuad(Vec3 a, Vec3 b, Vec3 c, Vec3 d) {
    if (!finite(a) || !finite(b) || !finite(c) || !finite(d)) violate("quad needs finite arguments");
    return {widen(a), widen(b), widen(c), widen(d)};
}

Counts count(const QuadShape&) { return {4, 2}; }

void emit(const Target& target, const QuadShape& shape) {
    Writer out(target);
    const Point3 normal = unit(cross(sub(shape.c, shape.a), sub(shape.d, shape.b)));
    const Point3 edge = sub(shape.b, shape.a), rise = sub(shape.d, shape.a);
    const double lean = dot(edge, normal);
    const Point3 tangent = unit({edge.x - normal.x * lean, edge.y - normal.y * lean, edge.z - normal.z * lean});
    const double w = dot(cross(normal, tangent), rise) >= 0.0 ? 1.0 : -1.0;
    const double fw = length(edge), fh = length(rise);
    if (!(fw > 0.0) || !(fh > 0.0)) violate("degenerate direction", ErrorReason::RuntimeViolation);
    const u32 first = out.vertex(shape.a, normal, tangent, 0.0, fh, fw, fh, w);
    out.vertex(shape.b, normal, tangent, fw, fh, fw, fh, w);
    out.vertex(shape.c, normal, tangent, fw, 0.0, fw, fh, w);
    out.vertex(shape.d, normal, tangent, 0.0, 0.0, fw, fh, w);
    out.quad(first);
}

TriShape checkTri(Vec3 a, Vec3 b, Vec3 c) {
    if (!finite(a) || !finite(b) || !finite(c)) violate("tri needs finite arguments");
    return {widen(a), widen(b), widen(c)};
}

Counts count(const TriShape&) { return {3, 1}; }

void emit(const Target& target, const TriShape& shape) {
    Writer out(target);
    const Point3 edge = sub(shape.b, shape.a), reach = sub(shape.c, shape.a);
    const Point3 area = cross(edge, reach);
    const Point3 normal = unit(area);
    const Point3 tangent = unit(edge);
    const double fw = length(edge);
    const double fh = length(area) / fw;
    const u32 first = out.vertex(shape.a, normal, tangent, 0.0, fh, fw, fh);
    out.vertex(shape.b, normal, tangent, fw, fh, fw, fh);
    out.vertex(shape.c, normal, tangent, dot(reach, tangent), 0.0, fw, fh);
    out.triangle(first, first + 1, first + 2);
}

// ---- vec3 values ------------------------------------------------------------

namespace {
Vec3 canonical(Vec3 v) { return {math::canonical(v.x), math::canonical(v.y), math::canonical(v.z)}; }
} // namespace

Vec3 vec3Add(Vec3 a, Vec3 b) { return canonical({a.x + b.x, a.y + b.y, a.z + b.z}); }
Vec3 vec3Sub(Vec3 a, Vec3 b) { return canonical({a.x - b.x, a.y - b.y, a.z - b.z}); }
Vec3 vec3Mul(Vec3 a, Vec3 b) { return canonical({a.x * b.x, a.y * b.y, a.z * b.z}); }
Vec3 vec3Div(Vec3 a, Vec3 b) { return canonical({a.x / b.x, a.y / b.y, a.z / b.z}); }
Vec3 vec3Scale(Vec3 a, f32 k) { return canonical({a.x * k, a.y * k, a.z * k}); }
Vec3 vec3Neg(Vec3 a) { return canonical({-a.x, -a.y, -a.z}); }

f32 vec3Len(Vec3 a) {
    const f32 xx = a.x * a.x;
    const f32 yy = a.y * a.y;
    const f32 zz = a.z * a.z;
    const f32 sum = xx + yy;
    return math::canonical(std::sqrt(sum + zz));
}

f32 vec3Dot(Vec3 a, Vec3 b) {
    const f32 xx = a.x * b.x;
    const f32 yy = a.y * b.y;
    const f32 zz = a.z * b.z;
    const f32 sum = xx + yy;
    return math::canonical(sum + zz);
}

Vec3 vec3Cross(Vec3 a, Vec3 b) {
    const f32 yz = a.y * b.z, zy = a.z * b.y;
    const f32 zx = a.z * b.x, xz = a.x * b.z;
    const f32 xy = a.x * b.y, yx = a.y * b.x;
    return canonical({yz - zy, zx - xz, xy - yx});
}

Vec3 vec3Normalize(Vec3 a) {
    if (!finite(a.x) || !finite(a.y) || !finite(a.z)) violate("vec3.normalize needs finite components");
    const double x = a.x, y = a.y, z = a.z;
    const double m = std::max({std::fabs(x), std::fabs(y), std::fabs(z)});
    if (m == 0.0) return Vec3{};
    const double sx = x / m, sy = y / m, sz = z / m;
    const double xx = sx * sx, yy = sy * sy, zz = sz * sz;
    const double scale = std::sqrt((xx + yy) + zz);
    return {static_cast<f32>(sx / scale), static_cast<f32>(sy / scale), static_cast<f32>(sz / scale)};
}

Vec3 vec3Mix(Vec3 a, Vec3 b, f32 t) {
    t = t > 0.f ? (t < 1.f ? t : 1.f) : 0.f;
    const auto component = [t](f32 x, f32 y) {
        const f32 mixed = x * (1.f - t) + y * t;
        const f32 lo = x < y ? x : y, hi = x < y ? y : x;
        return mixed < lo ? lo : mixed > hi ? hi : mixed;
    };
    return {component(a.x, b.x), component(a.y, b.y), component(a.z, b.z)};
}

} // namespace iprop::model
