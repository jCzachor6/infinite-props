#include "iprop/Geometry.hpp"
#include "iprop/VmInternals.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>

namespace iprop {

GeometryData::GeometryData(ResourceArena& arena, GeometryOrigin kind)
    : origin(kind), points(detail::ResourceAllocator<Vec2, 8>(arena)),
      sites(detail::ResourceAllocator<Vec2, 8>(arena)),
      edges(detail::ResourceAllocator<GeometryEdge, 32>(arena)),
      uses(detail::ResourceAllocator<GeometryEdgeUse, 8>(arena)),
      rings(detail::ResourceAllocator<GeometryRing, 48>(arena)),
      regions(detail::ResourceAllocator<GeometryRegion, 64>(arena)),
      holes(detail::ResourceAllocator<u32, 4>(arena)),
      neighbors(detail::ResourceAllocator<i32, 4>(arena)) {}

Vec2 GeometryData::start(GeometryEdgeUse use) const {
    const auto& edge = edges.at(use.edge);
    return points.at(use.reversed ? edge.second : edge.first);
}
Vec2 GeometryData::end(GeometryEdgeUse use) const {
    const auto& edge = edges.at(use.edge);
    return points.at(use.reversed ? edge.first : edge.second);
}
std::span<const u32> GeometryData::regionHoles(u32 region) const {
    const auto& value = regions.at(region);
    return std::span<const u32>(holes).subspan(value.holesBegin, value.holesCount);
}
std::span<const i32> GeometryData::regionNeighbors(u32 region) const {
    const auto& value = regions.at(region);
    return std::span<const i32>(neighbors).subspan(value.neighborsBegin, value.neighborsCount);
}

namespace detail {
namespace {
bool finite(Vec2 p) { return std::isfinite(p.x) && std::isfinite(p.y); }
[[noreturn]] void invalid(Execution& ex, const char* message) {
    ex.trap(message, ErrorReason::RuntimeViolation);
}
void limit(Execution& ex, std::size_t count, std::size_t maximum, const char* message) {
    if (count > maximum) invalid(ex, message);
}
bool same(Vec2 a, Vec2 b) { return a.x == b.x && a.y == b.y; }
f32 checkedFloat(Execution& ex, double value) {
    const auto rounded = static_cast<f32>(value);
    if (!std::isfinite(rounded)) invalid(ex, "geometry metadata is outside f32 range");
    return rounded;
}
} // namespace

GeometryBuilder::GeometryBuilder(Execution& ex, GeometryOrigin origin, std::shared_ptr<const GeometryData> source)
    : ex_(ex) {
    if (static_cast<u8>(origin) > static_cast<u8>(GeometryOrigin::Offset))
        invalid(ex, "invalid geometry origin");
    if ((origin == GeometryOrigin::Offset) != static_cast<bool>(source))
        invalid(ex, "derived geometry requires exactly one source");
    ex.spend(1);
    if (!ex.resources.canAllocate(512)) ex.trap("generation data exceeds 64 MiB", ErrorReason::OutOfMemory);
    ex.resources.resourceLifetime()->location = ex.activeInstruction;
    data_ = makeResource<GeometryData, 512>(ex.resources, ex.resources, origin);
    // Provenance points to the original topology, never a linked history of
    // every offset. Repeated operations cannot create recursive destruction.
    data_->source = source && source->source ? source->source : std::move(source);
}

u32 GeometryBuilder::point(Vec2 p) {
    if (!finite(p)) invalid(ex_, "geometry point must be finite");
    limit(ex_, data_->points.size() + 1, kMaxGeometryVertices, "geometry exceeds 65536 points");
    ex_.spend(1);
    data_->points.push_back(p);
    return static_cast<u32>(data_->points.size() - 1);
}
u32 GeometryBuilder::site(Vec2 p) {
    if (!finite(p)) invalid(ex_, "geometry site must be finite");
    limit(ex_, data_->sites.size() + 1, kMaxGeometrySites, "geometry exceeds 256 sites");
    ex_.spend(1);
    data_->sites.push_back(p);
    return static_cast<u32>(data_->sites.size() - 1);
}
u32 GeometryBuilder::edge(u32 first, u32 second, EdgeProvenance provenance) {
    if (first >= data_->points.size() || second >= data_->points.size() || first >= second ||
        same(data_->points[first], data_->points[second]))
        invalid(ex_, "geometry edge requires two distinct canonical endpoints");
    if (static_cast<u8>(provenance.origin) > static_cast<u8>(EdgeOrigin::Offset) ||
        (provenance.origin == EdgeOrigin::Offset &&
         (!data_->source || provenance.edge >= data_->source->edges.size())))
        invalid(ex_, "invalid geometry edge provenance");
    limit(ex_, data_->edges.size() + 1, kMaxGeometryVertices, "geometry exceeds 65536 edges");
    ex_.spend(1);
    data_->edges.push_back({first, second, provenance});
    return static_cast<u32>(data_->edges.size() - 1);
}
u32 GeometryBuilder::ring(std::span<const GeometryEdgeUse> edges, u32 parent) {
    if (edges.size() < 3 || edges.size() > kMaxRingVertices)
        invalid(ex_, "geometry ring requires 3..2048 vertices");
    limit(ex_, data_->uses.size() + edges.size(), kMaxGeometryVertices, "geometry exceeds 65536 ring vertices");
    if (parent != kNoGeometryIndex && parent >= data_->rings.size()) invalid(ex_, "invalid geometry ring parent");
    ex_.spend(static_cast<u64>(edges.size()) + 1);
    for (std::size_t i = 0; i < edges.size(); ++i) {
        if (edges[i].edge >= data_->edges.size() || edges[(i+1)%edges.size()].edge >= data_->edges.size())
            invalid(ex_, "geometry ring edge index out of range");
        const auto& a = data_->edges[edges[i].edge];
        const auto& b = data_->edges[edges[(i+1)%edges.size()].edge];
        if ((edges[i].reversed ? a.first : a.second) !=
            (edges[(i+1)%edges.size()].reversed ? b.second : b.first))
            invalid(ex_, "geometry ring is not closed");
    }
    GeometryRing ring;
    ring.begin = static_cast<u32>(data_->uses.size()); ring.count = static_cast<u32>(edges.size()); ring.parent = parent;
    ring.hole = parent != kNoGeometryIndex && !data_->rings[parent].hole;
    data_->uses.insert(data_->uses.end(), edges.begin(), edges.end());
    data_->rings.push_back(ring);
    return static_cast<u32>(data_->rings.size() - 1);
}
u32 GeometryBuilder::region(u32 outer, i32 sourceId, std::span<const u32> holes, std::span<const i32> neighbors) {
    limit(ex_, data_->regions.size() + 1, kMaxGeometryRegions, "geometry exceeds 256 regions");
    if (outer >= data_->rings.size() || data_->rings[outer].hole) invalid(ex_, "invalid outer ring");
    limit(ex_, data_->holes.size() + holes.size(), data_->rings.size(), "too many geometry holes");
    limit(ex_, neighbors.size(), kMaxGeometryRegions - 1, "too many geometry neighbors");
    ex_.spend(1 + static_cast<u64>(holes.size()) + neighbors.size());
    for (const auto hole : holes)
        if (hole >= data_->rings.size() || !data_->rings[hole].hole || data_->rings[hole].parent != outer)
            invalid(ex_, "hole must be an immediate child of its outer ring");
    GeometryRegion region;
    region.outer = outer; region.sourceId = sourceId;
    region.holesBegin = static_cast<u32>(data_->holes.size()); region.holesCount = static_cast<u32>(holes.size());
    region.neighborsBegin = static_cast<u32>(data_->neighbors.size()); region.neighborsCount = static_cast<u32>(neighbors.size());
    data_->holes.insert(data_->holes.end(), holes.begin(), holes.end());
    data_->neighbors.insert(data_->neighbors.end(), neighbors.begin(), neighbors.end());
    data_->regions.push_back(region);
    return static_cast<u32>(data_->regions.size() - 1);
}

std::shared_ptr<const GeometryData> GeometryBuilder::finish() && {
    auto data = std::move(data_);
    if (!data) throw std::logic_error("geometry builder is consumed");
    // Reserve bounded validation work before scratch allocation or traversal.
    const auto edgeCount = static_cast<u64>(data->edges.size());
    ex_.spend(4 * edgeCount * (1 + std::bit_width(edgeCount)) +
        8 * static_cast<u64>(data->uses.size()) + 4 * data->rings.size() +
        data->neighbors.size() + 2 * data->holes.size() + data->regions.size());
    const ResourceReservation workspace(ex_.resources, 256);
    ResourceVector<u32, 4> sorted{ResourceAllocator<u32, 4>(ex_.resources)};
    sorted.reserve(data->edges.size());
    for (u32 i = 0; i < data->edges.size(); ++i) sorted.push_back(i);
    const auto key = [&](u32 i) { const auto& e = data->edges[i]; return std::pair(e.first, e.second); };
    // Iterative heapsort has constant automatic workspace and a fixed
    // comparison bound; no library-dependent recursive sort stack is hidden
    // outside the arena. Sorting is validation only, not geometry ordering.
    const auto sift = [&](std::size_t root, std::size_t count) {
        while (root < count / 2) {
            auto child = root * 2 + 1;
            if (child + 1 < count && key(sorted[child]) < key(sorted[child+1])) ++child;
            if (!(key(sorted[root]) < key(sorted[child]))) break;
            std::swap(sorted[root], sorted[child]); root = child;
        }
    };
    for (auto root = sorted.size() / 2; root > 0; --root) sift(root-1, sorted.size());
    for (auto end = sorted.size(); end > 1; --end) {
        std::swap(sorted[0], sorted[end-1]); sift(0, end-1);
    }
    for (std::size_t i = 1; i < sorted.size(); ++i)
        if (key(sorted[i-1]) == key(sorted[i])) invalid(ex_, "duplicate geometry edge");
    ResourceVector<u8, 1> edgeUses(data->edges.size(), u8{0}, ResourceAllocator<u8, 1>(ex_.resources));
    ResourceVector<u8, 1> ringOwners(data->rings.size(), u8{0}, ResourceAllocator<u8, 1>(ex_.resources));
    for (auto& ring : data->rings) {
        double twiceArea = 0, momentX = 0, momentY = 0;
        for (u32 i = 0; i < ring.count; ++i) {
            const auto use = data->uses[ring.begin + i];
            auto& used = edgeUses[use.edge];
            const u8 direction = use.reversed ? 2 : 1;
            if (used & direction) invalid(ex_, "geometry edge is reused in the same direction");
            used |= direction;
            const auto a = data->start(use), b = data->end(use);
            const auto reference = data->start(data->uses[ring.begin]);
            const double ax=static_cast<double>(a.x)-reference.x, ay=static_cast<double>(a.y)-reference.y;
            const double bx=static_cast<double>(b.x)-reference.x, by=static_cast<double>(b.y)-reference.y;
            const double cross = ax*by-bx*ay;
            twiceArea += cross;
            momentX += (ax+bx)*cross;
            momentY += (ay+by)*cross;
        }
        if (!std::isfinite(twiceArea) || (ring.hole ? twiceArea >= 0 : twiceArea <= 0))
            invalid(ex_, "geometry ring has zero area or incorrect orientation");
        ring.area = twiceArea * 0.5;
        const auto reference=data->start(data->uses[ring.begin]);
        ring.momentX = momentX / 6.0 + static_cast<double>(reference.x)*ring.area;
        ring.momentY = momentY / 6.0 + static_cast<double>(reference.y)*ring.area;
    }
    if (std::find(edgeUses.begin(), edgeUses.end(), u8{0}) != edgeUses.end())
        invalid(ex_, "geometry contains an unused edge");
    if (data->origin == GeometryOrigin::Diagram && data->sites.size() != data->regions.size())
        invalid(ex_, "diagram requires one region per site");
    for (u32 index = 0; index < data->regions.size(); ++index) {
        auto& region = data->regions[index];
        const auto& outer = data->rings[region.outer];
        if (ringOwners[region.outer]++) invalid(ex_, "geometry ring belongs to multiple regions");
        double area = outer.area, momentX = outer.momentX, momentY = outer.momentY;
        for (const auto hole : data->regionHoles(index)) {
            if (ringOwners[hole]++) invalid(ex_, "geometry hole belongs to multiple regions");
            area += data->rings[hole].area;
            momentX += data->rings[hole].momentX; momentY += data->rings[hole].momentY;
        }
        if (!(area > 0)) invalid(ex_, "geometry region must have positive area");
        region.area = checkedFloat(ex_, area);
        if (region.area == 0) invalid(ex_, "geometry region area is outside f32 range");
        region.center = {checkedFloat(ex_, momentX / area), checkedFloat(ex_, momentY / area)};
        Vec2 lo = data->start(data->uses[outer.begin]), hi = lo;
        for (u32 i = 1; i < outer.count; ++i) {
            const auto p = data->start(data->uses[outer.begin + i]);
            lo = {std::min(lo.x, p.x), std::min(lo.y, p.y)};
            hi = {std::max(hi.x, p.x), std::max(hi.y, p.y)};
        }
        region.boundsOrigin = lo;
        region.boundsSize = {checkedFloat(ex_, static_cast<double>(hi.x) - lo.x),
                             checkedFloat(ex_, static_cast<double>(hi.y) - lo.y)};
        i32 previous = -1;
        for (const auto neighbor : data->regionNeighbors(index)) {
            if (neighbor < 0 || neighbor >= static_cast<i32>(data->regions.size()) ||
                neighbor == static_cast<i32>(index) || neighbor <= previous)
                invalid(ex_, "geometry neighbors must be sorted distinct valid region indices");
            previous = neighbor;
        }
    }
    if (std::find(ringOwners.begin(), ringOwners.end(), u8{0}) != ringOwners.end())
        invalid(ex_, "geometry contains an unowned ring");
    return data;
}

} // namespace detail
} // namespace iprop
