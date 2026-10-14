#pragma once

#include "iprop/Outputs.hpp"
#include "iprop/math/MinMax.hpp"

#include <optional>

namespace iprop {

struct RasterPoint {
    i32 x = 0;
    i32 y = 0;
    constexpr bool operator==(const RasterPoint&) const = default;
};
struct RasterPoint64 { i64 x = 0, y = 0; };

// The eight rectangle symmetries. Construction validates the wire kind;
// pixel indices and continuous edge coordinates deliberately use distinct APIs.
class D4Transform {
public:
    constexpr D4Transform() = default;

    static constexpr std::optional<D4Transform> fromKind(u8 kind) {
        return kind < 8 ? std::optional(D4Transform(kind)) : std::nullopt;
    }
    static constexpr std::optional<D4Transform> fromParameters(i32 rot, i32 mx, i32 my) {
        if (rot < 0 || rot > 270 || rot % 90 != 0 || mx < 0 || mx > 1 || my < 0 || my > 1)
            return std::nullopt;
        auto result = D4Transform(static_cast<u8>(rot / 90));
        if (mx) result = result.compose(D4Transform(4));
        if (my) result = result.compose(D4Transform(5));
        return result;
    }

    constexpr u8 kind() const { return kind_; }
    constexpr bool swapsAxes() const { return kind_ == 1 || kind_ == 3 || kind_ >= 6; }
    constexpr Vec2 mapExtent(Vec2 extent) const {
        return swapsAxes() ? Vec2{extent.y, extent.x} : extent;
    }
    constexpr RasterPoint mapExtent(RasterPoint extent) const {
        return swapsAxes() ? RasterPoint{extent.y, extent.x} : extent;
    }
    constexpr Vec2 mapPoint(Vec2 p, Vec2 extent) const { return map(p, extent); }
    constexpr RasterPoint mapPixel(RasterPoint p, RasterPoint extent) const {
        return map(p, RasterPoint{extent.x - 1, extent.y - 1});
    }
    constexpr RasterPoint64 mapPixelWide(RasterPoint64 p, RasterPoint extent) const {
        return map(p, RasterPoint64{static_cast<i64>(extent.x) - 1,
                                  static_cast<i64>(extent.y) - 1});
    }
    OutputRect mapRect(OutputRect r, Vec2 extent) const {
        if (kind_ == 0) return r; // identity transports the original bits
        const auto a = mapPoint({r.x, r.y}, extent);
        const auto b = mapPoint({r.x + r.w, r.y + r.h}, extent);
        const f32 x = math::f32MinMax(a.x, b.x, false);
        const f32 y = math::f32MinMax(a.y, b.y, false);
        return {x, y, math::f32MinMax(a.x, b.x, true) - x,
                      math::f32MinMax(a.y, b.y, true) - y};
    }

    // Apply this transform, then next. The linear action uniquely identifies
    // each symmetry; extent-dependent translations follow from the same map.
    constexpr D4Transform compose(D4Transform next) const {
        const auto x = next.map(map(RasterPoint{1,0}, RasterPoint{}), RasterPoint{});
        const auto y = next.map(map(RasterPoint{0,1}, RasterPoint{}), RasterPoint{});
        for (u8 k = 0; k < 8; ++k) {
            const D4Transform candidate(k);
            if (candidate.map(RasterPoint{1,0}, RasterPoint{}) == x &&
                candidate.map(RasterPoint{0,1}, RasterPoint{}) == y) return candidate;
        }
        return {}; // all compositions are in D4
    }
    constexpr D4Transform inverse() const {
        return D4Transform(kind_ == 1 ? 3 : kind_ == 3 ? 1 : kind_);
    }
    constexpr bool operator==(const D4Transform&) const = default;

private:
    u8 kind_ = 0;
    explicit constexpr D4Transform(u8 kind) : kind_(kind) {}
    template<class Point>
    constexpr Point map(Point p, Point e) const {
        switch (kind_) {
        case 0: return p;
        case 1: return {p.y, e.x - p.x};
        case 2: return {e.x - p.x, e.y - p.y};
        case 3: return {e.y - p.y, p.x};
        case 4: return {e.x - p.x, p.y};
        case 5: return {p.x, e.y - p.y};
        case 6: return {p.y, p.x};
        default: return {e.y - p.y, e.x - p.x};
        }
    }
};

} // namespace iprop
