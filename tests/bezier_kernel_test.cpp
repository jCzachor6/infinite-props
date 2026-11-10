#include <catch2/catch_test_macros.hpp>
#include "iprop/math/Bezier.hpp"
#include "support/bezier_corpus.hpp"

#include <array>
#include <cmath>
#include <limits>

using namespace iprop;

TEST_CASE("prepared Bezier queries retain the unprepared kernel's exact bits") {
    for (bool cubic : {false,true}) {
        const auto hash = test_support::bezierCorpusHash(cubic,
            [](bool degree, const auto& c, Vec2 p) {
                const auto kernel = degree ? math::BezierKernel::cubic(c[0],c[1],c[2],c[3])
                                           : math::BezierKernel::quadratic(c[0],c[1],c[2]);
                return kernel.distanceTo(p);
            });
        // 104 curves x 33 query points per degree, recorded from 9bda0c2.
        CHECK(hash == (cubic ? 0x2bf11bf7545c85e5ull : 0x3d08a31c26764c73ull));
    }
}

TEST_CASE("a prepared kernel is reusable for independent distance queries") {
    const auto line = math::BezierKernel::quadratic({0,0},{2,0},{4,0});
    const auto point = math::BezierKernel::cubic({2,3},{2,3},{2,3},{2,3});
    CHECK(line.distanceTo({2,3}) == 3.f);
    CHECK(point.distanceTo({5,7}) == 5.f);
    CHECK(line.distanceTo({2,-3}) == 3.f);
    CHECK(point.distanceTo({2,3}) == 0.f);
    CHECK(line.distanceTo({-1,0}) == 1.f);
    CHECK(line.distanceTo({2,3}) == 3.f);
}

TEST_CASE("a stroke test is the distance comparison, at and beside the exact radius") {
    const f32 infinity = std::numeric_limits<f32>::infinity();
    for (bool cubic : {false,true}) {
        std::size_t disagreements = 0, inside = 0;
        (void)test_support::bezierCorpusHash(cubic, [&](bool degree, const auto& c, Vec2 p) {
            const auto kernel = degree ? math::BezierKernel::cubic(c[0],c[1],c[2],c[3])
                                       : math::BezierKernel::quadratic(c[0],c[1],c[2]);
            const f32 distance = kernel.distanceTo(p);
            for (const f32 radius : {distance, std::nextafter(distance, 0.f), std::nextafter(distance, infinity),
                                     distance * 0.5f, distance * 2.f, 0.f, -1.f, 1e-30f, 1e30f, infinity,
                                     std::numeric_limits<f32>::quiet_NaN()}) {
                const bool expected = distance <= radius;
                if (kernel.within(p, radius) != expected) ++disagreements;
                if (expected) ++inside;
            }
            return distance;
        });
        CHECK(disagreements == 0);
        CHECK(inside > 1000);
    }
}

TEST_CASE("covering boxes hold every point a stroke of that radius can reach") {
    // Probes sit on a ring just inside and just outside the radius around
    // points of the curve, at three coordinate scales.
    for (bool cubic : {false,true}) for (const f32 scale : {1.f/64.f, 1.f, 256.f}) {
        std::size_t escaped = 0, reached = 0, bounded = 0;
        (void)test_support::bezierCorpusHash(cubic, [&](bool degree, auto c, Vec2 query) {
            for (auto& p : c) p = {p.x * scale, p.y * scale};
            const auto kernel = degree ? math::BezierKernel::cubic(c[0],c[1],c[2],c[3])
                                       : math::BezierKernel::quadratic(c[0],c[1],c[2]);
            for (const f32 radius : {0.25f, 1.f, 3.5f, 40.f}) {
                std::array<math::BezierKernel::Box, 16> boxes;
                const auto count = kernel.cover(boxes, radius);
                if (count == 0) continue; // degenerate or nonfinite: nothing may be skipped
                ++bounded;
                const auto covered = [&](Vec2 p) {
                    for (std::size_t i = 0; i < count; ++i)
                        if (boxes[i].left <= p.x && p.x <= boxes[i].right && boxes[i].top <= p.y && p.y <= boxes[i].bottom)
                            return true;
                    return false;
                };
                for (int step = 0; step <= 12; ++step) {
                    const double t = step / 12.0, s = 1.0 - t;
                    const auto at = [&](auto coordinate) {
                        return degree ? s*s*s*coordinate(c[0]) + 3*s*s*t*coordinate(c[1]) + 3*s*t*t*coordinate(c[2]) + t*t*t*coordinate(c[3])
                                      : s*s*coordinate(c[0]) + 2*s*t*coordinate(c[1]) + t*t*coordinate(c[2]);
                    };
                    const double x = at([](Vec2 p) { return static_cast<double>(p.x); });
                    const double y = at([](Vec2 p) { return static_cast<double>(p.y); });
                    for (int direction = 0; direction < 8; ++direction) for (const double reach : {0.97, 1.0, 1.03}) {
                        const double angle = direction * 0.7853981633974483;
                        const Vec2 p{static_cast<f32>(x + radius * reach * std::cos(angle)),
                                     static_cast<f32>(y + radius * reach * std::sin(angle))};
                        if (!kernel.within(p, radius)) continue;
                        ++reached;
                        if (!covered(p)) ++escaped;
                    }
                }
            }
            return query.x;
        });
        CHECK(escaped == 0);
        CHECK(reached > 10000);
        CHECK(bounded > 300);
    }
}
