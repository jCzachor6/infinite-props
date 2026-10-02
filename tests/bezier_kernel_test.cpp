#include <catch2/catch_test_macros.hpp>
#include "iprop/math/Bezier.hpp"
#include "support/bezier_corpus.hpp"

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
