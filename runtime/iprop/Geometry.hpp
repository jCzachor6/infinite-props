#pragma once

#include "iprop/ResourceMemory.hpp"

#include <span>
#include <optional>

namespace iprop {

inline constexpr u32 kMaxGeometrySites = 256;
inline constexpr u32 kMaxGeometryRegions = 256;
inline constexpr u32 kMaxRingVertices = 2048;
inline constexpr u32 kMaxGeometryVertices = 65536;
inline constexpr u32 kNoGeometryIndex = 0xffffffffu;

enum class GeometryOrigin : u8 { Diagram, Contours, Offset };
enum class EdgeOrigin : u8 { Boundary, Diagram, Contour, Offset };

// Direction-independent ancestry survives offsetting and fragmentation. A
// derived edge refers to its parent's edge; source IDs identify original cells.
struct EdgeProvenance {
    u32 edge = kNoGeometryIndex;
    i32 source = -1, neighbor = -1;
    EdgeOrigin origin = EdgeOrigin::Boundary;
};
struct GeometryEdge {
    u32 first = 0, second = 0; // canonical vertex-index order: first < second
    EdgeProvenance provenance;
};
struct GeometryEdgeUse {
    u32 edge = 0;
    bool reversed = false;
};
struct GeometryRing {
    u32 begin = 0, count = 0, parent = kNoGeometryIndex;
    bool hole = false;
    // Binary64 signed area and first moments, cached once in traversal order.
    double area = 0, momentX = 0, momentY = 0;
};
struct GeometryRegion {
    u32 outer = 0, holesBegin = 0, holesCount = 0;
    u32 neighborsBegin = 0, neighborsCount = 0;
    i32 sourceId = -1;
    f32 area = 0;
    Vec2 center, boundsOrigin, boundsSize;
};

// Diagram, contours and regions are distinct VM alternatives over exactly one
// immutable store. Ring uses, holes and neighbors are flat index ranges, never
// nested unaccounted containers. Algorithms build a new store only when the
// geometry actually changes. Derived geometry retains its ancestry by reference.
struct GeometryData {
    GeometryOrigin origin;
    std::shared_ptr<const GeometryData> source;
    detail::ResourceVector<Vec2, 8> points, sites;
    detail::ResourceVector<GeometryEdge, 32> edges;
    detail::ResourceVector<GeometryEdgeUse, 8> uses;
    detail::ResourceVector<GeometryRing, 48> rings;
    detail::ResourceVector<GeometryRegion, 64> regions;
    detail::ResourceVector<u32, 4> holes;
    detail::ResourceVector<i32, 4> neighbors;

    explicit GeometryData(ResourceArena& arena, GeometryOrigin kind);
    GeometryData(const GeometryData&) = delete;
    GeometryData& operator=(const GeometryData&) = delete;
    Vec2 start(GeometryEdgeUse use) const;
    Vec2 end(GeometryEdgeUse use) const;
    std::span<const u32> regionHoles(u32 region) const;
    std::span<const i32> regionNeighbors(u32 region) const;
};

struct RegionSource {
    std::shared_ptr<const GeometryData> geometry;
    u32 region = 0;
};
// Original source pixel coordinates, cached once at capture. Raster anchors
// retain exact half-pixel values even for scales such as 3 pixels/local unit.
struct DynamicPartFrame {
    double centerX=0, centerY=0, left=0, top=0, right=0, bottom=0;
    Vec2 scale{1,1};
};
// Source geometry/raster, placement and appearance are retained separately.
// These are internal immutable records, not an exported ABI layout.
struct DynamicPartData {
    std::variant<RegionSource, MaskValue, LayerValue> source;
    i32 id = -1, sourceId = -1;
    Vec2 center, boundsOrigin, boundsSize;
    D4Transform transform;
    RasterPoint translation; // linear D4 about source zero, then integer pixels
    std::optional<Color> fill;
    std::optional<LayerValue> texture;
    DynamicPartFrame original;
};
struct DynamicPartsData {
    detail::ResourceVector<Part2dValue, 16> parts;
    explicit DynamicPartsData(ResourceArena& arena) : parts(detail::ResourceAllocator<Part2dValue, 16>(arena)) {}
};
static_assert(sizeof(GeometryData) + 64 <= 512);
static_assert(sizeof(DynamicPartData) + 64 <= 256);
static_assert(sizeof(DynamicPartsData) + 64 <= 128);
static_assert(sizeof(Part2dValue) <= 16);

namespace detail {
struct Execution;

// A builder owns unpublished storage. Dropping it (including a trap) refunds
// headers, capacities, scratch and dependencies. finish consumes it even on
// failure, validates topology and caches metadata before returning a const view.
class GeometryBuilder {
public:
    GeometryBuilder(Execution& execution, GeometryOrigin origin,
                    std::shared_ptr<const GeometryData> source = {});
    GeometryBuilder(const GeometryBuilder&) = delete;
    GeometryBuilder& operator=(const GeometryBuilder&) = delete;
    GeometryBuilder(GeometryBuilder&&) = default;
    GeometryBuilder& operator=(GeometryBuilder&&) = delete;
    u32 point(Vec2 point);
    u32 site(Vec2 point);
    u32 edge(u32 first, u32 second, EdgeProvenance provenance = {});
    u32 ring(std::span<const GeometryEdgeUse> edges, u32 parent = kNoGeometryIndex);
    u32 region(u32 outer, i32 sourceId, std::span<const u32> holes = {},
               std::span<const i32> neighbors = {});
    std::shared_ptr<const GeometryData> finish() &&;
private:
    Execution& ex_;
    std::shared_ptr<GeometryData> data_;
};

} // namespace detail
} // namespace iprop
