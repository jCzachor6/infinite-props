#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include "Compiler.hpp"
#include "iprop/Debug.hpp"
#include "iprop/VM.hpp"
#include "iprop/math/Distribution.hpp"
#include "ips/Parser.hpp"
#include "ips/Printer.hpp"
#include "ips/Semantics.hpp"
#include "ips/SourceError.hpp"

#include "support/compiler_support.hpp"
#include "support/test_support.hpp"

// Distributions (spec v0.5 §4). Kernel references come from 60-digit mpmath;
// the draw vectors from an independent binary64 reference implementation of
// the spec algorithms.

using namespace iprop;

namespace {
constexpr u8 kStreamPush = 0x60, kGaussian = 0xAA, kTriangular = 0xAB, kBeta = 0xAC;
constexpr u8 kIntWith = 0xAD, kFloatWith = 0xAE;

double relative(double actual, double expected) {
    return expected == 0.0 ? std::fabs(actual) : std::fabs(actual - expected) / std::fabs(expected);
}

std::vector<u8> compileSource(const std::string& source, bool optimize) {
    const auto program = ips::parse(source);
    ips::checkAsset(program, {});
    return compileProgram(program, {}, optimize);
}

GenerationResult generate(const std::vector<u8>& bytes, u32 seed, u32 variant = 0) {
    const auto loaded = loadFile(bytes);
    validate(loaded);
    VM vm(loaded, GenerationInputs{seed, 0, 0, {{"v", {InputType::U32, {}, 0.f, variant}}}});
    vm.run();
    return vm.result();
}

std::vector<f32> xs(const GenerationResult& result, std::size_t output = 0) {
    std::vector<f32> values;
    for (const auto& point : result.outputs.at(output).points) values.push_back(point.x);
    return values;
}

std::vector<u32> pointBits(const GenerationResult& result) {
    std::vector<u32> bits;
    for (const auto& output : result.outputs)
        for (const auto& point : output.points) {
            bits.push_back(std::bit_cast<u32>(point.x));
            bits.push_back(std::bit_cast<u32>(point.y));
        }
    return bits;
}

std::string sourceError(const std::string& source) {
    try {
        ips::checkAsset(ips::parse(source), {});
    } catch (const ips::SourceError& error) {
        return error.message;
    }
    FAIL("expected a source error");
    return {};
}

std::string format(const std::string& source) { return ips::formatProgram(ips::parse(source)); }

// Largest distance between the sample CDF and a reference CDF.
double kolmogorov(std::vector<f32> sample, const auto& cdf) {
    std::sort(sample.begin(), sample.end());
    double worst = 0.0;
    const double n = static_cast<double>(sample.size());
    for (std::size_t i = 0; i < sample.size(); ++i) {
        const double f = cdf(static_cast<double>(sample[i]));
        worst = std::max({worst, std::fabs(f - static_cast<double>(i) / n), std::fabs(f - (i + 1.0) / n)});
    }
    return worst;
}

double normal(double z) { return 0.5 * std::erfc(-z / std::sqrt(2.0)); }

// Draws `count` values of `expression` into the list output `p`.
std::string drawProgram(const std::string& setup, const std::string& expression, int count) {
    return "asset t { size 1x1 output list point p draw { " + setup + " for i in 0.." + std::to_string(count) +
           " { p = {" + expression + ", 0} } } }";
}
} // namespace

TEST_CASE("distribution kernels match high-precision references") {
    const std::array<std::pair<double, double>, 9> cdf{{{-37.0, 5.7255712225245768227e-300},
        {-20.0, 2.7536241186062336951e-89}, {-8.0, 6.2209605742717841235e-16}, {-1.5, 0.066807201268858066004},
        {-0.25, 0.40129367431707627576}, {0.0, 0.5}, {0.3, 0.61791142218895263307},
        {2.9, 0.99813418669961596152}, {8.0, 0.9999999999999993779}}};
    for (const auto& [z, expected] : cdf) {
        INFO(z);
        u32 steps = 0;
        CHECK(relative(math::normalCdf(z, steps), expected) < 2e-14);
        CHECK(steps > 0);
        CHECK(steps <= 97);
    }
    const std::array<std::pair<double, double>, 7> quantile{{{1e-300, -37.047096299361199237},
        {1e-10, -6.3613409024040562047}, {0.02, -2.0537489106318230529}, {0.3, -0.52440051270804078404},
        {0.5, 0.0}, {0.97, 1.8807936081512509389}, {0.999999, 4.7534243088170877657}}};
    for (const auto& [p, expected] : quantile) {
        INFO(p);
        CHECK(relative(math::normalQuantile(p), expected) < 1e-14);
    }
    const std::array<std::pair<double, double>, 6> gamma{{{1e-30, 69.077552789821370521},
        {0.3, 1.0957979948180755217}, {1.0, 0.0}, {2.5, 0.28468287047291915963},
        {50.0, 144.56574394634488601}, {1e5, 1051287.7089736568949}}};
    for (const auto& [x, expected] : gamma) {
        INFO(x);
        CHECK(std::fabs(math::logGamma(x) - expected) < 4e-15 * std::max(1.0, std::fabs(expected)));
    }
    const std::array<std::array<double, 4>, 5> beta{{{2.0, 5.0, 0.3, 0.579825},
        {0.5, 0.5, 1e-6, 0.00063661987847092448418}, {200.0, 300.0, 0.4, 0.50242861631993159158},
        {0.2, 3.0, 0.9, 0.99990630764302949758}, {30.0, 0.3, 0.5, 4.5602445173574145419e-11}}};
    for (const auto& [a, b, t, expected] : beta) {
        INFO(a << " " << b << " " << t);
        u32 steps = 0;
        CHECK(relative(math::incompleteBeta(a, b, t, steps), expected) < 1e-12);
    }
    for (const double x : {1e-300, 0.5, 1.0, 2.0, 1e300})
        CHECK(relative(math::pinnedLog(x), std::log(x)) < 4e-16 * std::max(1.0, 1.0 / std::fabs(std::log(x))));
    for (const double x : {-700.0, -1.0, 0.0, 0.5, 700.0}) CHECK(relative(math::pinnedExp(x), std::exp(x)) < 1e-15);
    CHECK(math::pinnedExp(-746.0) == 0.0);
    CHECK(math::pinnedExp(710.0) == std::numeric_limits<double>::infinity());
}

namespace {
struct DrawRow {
    u32 seed;
    i64 expected; // i32 value or f32 bits
    u32 steps;
};
struct DrawVector {
    DistributionFamily family;
    std::vector<f32> parameters;
    double lo, hi;
    bool integer;
    std::array<DrawRow, 4> rows;
};
} // namespace

TEST_CASE("draws with a distribution match the pinned vectors, stream and fuel") {
    using F = DistributionFamily;
    const std::vector<DrawVector> vectors = {
        {F::Gaussian, {0.0f, 2.0f}, -5, 5, true, {{{0u, -4, 134}, {7u, -1, 134}, {42u, 3, 134}, {3735928559u, -1, 134}}}},
        {F::Gaussian, {0.0f, 2.0f}, -5.0, 5.0, false, {{{0u, 0xc08cd865, 134}, {7u, 0xbfaaa38c, 134}, {42u, 0x4038a278, 134}, {3735928559u, 0xbface695, 134}}}},
        {F::Gaussian, {0.0f, 2.0f}, 20.0, 30.0, false, {{{0u, 0x41a00329, 33}, {7u, 0x41a07436, 33}, {42u, 0x41a42d67, 33}, {3735928559u, 0x41a072ae, 33}}}},
        {F::Gaussian, {1.85f, 0.08f}, 1.6, 2.0, false, {{{0u, 0x3fd45214, 71}, {7u, 0x3fe5a64d, 71}, {42u, 0x3ffa1280, 71}, {3735928559u, 0x3fe58f15, 71}}}},
        {F::Gaussian, {25.0f, 1.0f}, -5, 5, true, {{{0u, 5, 23}, {7u, 5, 23}, {42u, 5, 23}, {3735928559u, 5, 23}}}},
        {F::Gaussian, {0.5f, 0.15f}, 0.0, 1.0, false, {{{0u, 0x3e0f4a94, 84}, {7u, 0x3ecc1c1a, 84}, {42u, 0x3f38c87e, 84}, {3735928559u, 0x3ecb6b44, 84}}}},
        {F::Triangular, {-5.0f, 0.0f, 5.0f}, -5, 5, true, {{{0u, -4, 0}, {7u, -1, 0}, {42u, 3, 0}, {3735928559u, -1, 0}}}},
        {F::Triangular, {0.0f, 0.0f, 4.0f}, 0.0, 4.0, false, {{{0u, 0x3c7ef19b, 0}, {7u, 0x3f08d823, 0}, {42u, 0x403ca396, 0}, {3735928559u, 0x3f07296a, 0}}}},
        {F::Triangular, {0.0f, 4.0f, 4.0f}, 3.9, 10.0, false, {{{0u, 0x4079a67c, 0}, {7u, 0x407b3626, 0}, {42u, 0x407f8fe2, 0}, {3735928559u, 0x407b3174, 0}}}},
        {F::Beta, {2.0f, 5.0f, 0.0f, 1.0f}, 0.0, 1.0, false, {{{0u, 0x3cc05859, 21}, {7u, 0x3e24c7d6, 32}, {42u, 0x3f0cc90c, 12}, {3735928559u, 0x3e238f64, 32}}}},
        {F::Beta, {5.0f, 2.0f, 0.0f, 1.0f}, 0.0, 1.0, false, {{{0u, 0x3e8ec558, 12}, {7u, 0x3f1c3372, 12}, {42u, 0x3f6cbe22, 31}, {3735928559u, 0x3f1bbfd3, 12}}}},
        {F::Beta, {0.5f, 0.5f, 0.0f, 1.0f}, 0.0, 1.0, false, {{{0u, 0x391bff35, 20}, {7u, 0x3e154bf7, 39}, {42u, 0x3f7cfbe3, 27}, {3735928559u, 0x3e12137b, 39}}}},
        {F::Beta, {2.0f, 5.0f, 1.6f, 2.0f}, 1.6, 2.0, false, {{{0u, 0x3fce008e, 21}, {7u, 0x3fd509fe, 32}, {42u, 0x3fe8f503, 12}, {3735928559u, 0x3fd4fa5f, 32}}}},
        {F::Beta, {0.2f, 3.0f, 0.0f, 1.0f}, 0.0, 1.0, false, {{{0u, 0x2cf7dedd, 15}, {7u, 0x397ca655, 19}, {42u, 0x3e89b18f, 76}, {3735928559u, 0x396e8984, 19}}}},
        {F::Beta, {4.0f, 4.0f, 0.0f, 10.0f}, 0, 10, true, {{{0u, 1, 25}, {7u, 4, 25}, {42u, 8, 25}, {3735928559u, 4, 25}}}},
        {F::Beta, {200.0f, 300.0f, 0.0f, 1.0f}, 0.3, 0.5, false, {{{0u, 0x3eb20de0, 150}, {7u, 0x3ec52cbb, 181}, {42u, 0x3edd7d40, 196}, {3735928559u, 0x3ec51317, 181}}}},
        {F::Gaussian, {0.0f, 1.0f}, 3.0, 3.0, false, {{{0u, 0x40400000, 0}, {7u, 0x40400000, 0}, {42u, 0x40400000, 0}, {3735928559u, 0x40400000, 0}}}},
        {F::Triangular, {0.0f, 1.0f, 2.0f}, 1, 1, true, {{{0u, 1, 0}, {7u, 1, 0}, {42u, 1, 0}, {3735928559u, 1, 0}}}},
    };
    for (const auto& vector : vectors) {
        for (const auto& row : vector.rows) {
            INFO(static_cast<int>(vector.family) << " " << vector.lo << " to " << vector.hi << " seed " << row.seed);
            test_support::Prog prog;
            prog.I(static_cast<i32>(row.seed)).emit(kStreamPush);
            if (vector.integer) prog.I(static_cast<i32>(vector.lo)).I(static_cast<i32>(vector.hi));
            else prog.F(static_cast<f32>(vector.lo)).F(static_cast<f32>(vector.hi));
            for (const f32 parameter : vector.parameters) prog.F(parameter);
            prog.emit(vector.family == F::Gaussian ? kGaussian : vector.family == F::Triangular ? kTriangular : kBeta);
            prog.emit(vector.integer ? kIntWith : kFloatWith).emit(0x66); // stream.state
            const auto bytes = test_support::buildProgFile(prog);
            const auto loaded = loadFile(bytes);
            VM vm(loaded, {});
            vm.run();
            const auto& stack = vm.debugStack();
            REQUIRE(stack.size() == 2);
            if (vector.integer) CHECK(std::get<i32>(stack[0]) == row.expected);
            else CHECK(std::bit_cast<u32>(std::get<f32>(stack[0])) == static_cast<u32>(row.expected));
            CHECK(static_cast<u32>(std::get<i32>(stack[1])) == row.seed + 0x9e3779b9u);
            // seed, push, two bounds, parameters, constructor, draw, state
            CHECK(vm.fuelBudget() - vm.fuel() == 7 + vector.parameters.size() + row.steps);
        }
    }
}

TEST_CASE("constructors validate their parameters and draws trap on invalid ranges") {
    using test_support::expectProgTrap;
    using test_support::Prog;
    constexpr f32 inf = std::numeric_limits<f32>::infinity(), nan = std::numeric_limits<f32>::quiet_NaN();
    for (const auto& [mean, sigma] : std::array<std::pair<f32, f32>, 5>{{{0.f, 0.f}, {0.f, -1.f}, {nan, 1.f}, {0.f, inf}, {inf, 1.f}}})
        expectProgTrap(Prog{}.F(mean).F(sigma).emit(kGaussian), "gaussian needs a finite mean and a finite sigma > 0");
    for (const auto& [lo, peak, hi] : std::array<std::array<f32, 3>, 4>{{{1.f, 1.f, 1.f}, {0.f, 2.f, 1.f}, {0.f, -1.f, 1.f}, {0.f, nan, 1.f}}})
        expectProgTrap(Prog{}.F(lo).F(peak).F(hi).emit(kTriangular), "triangular needs finite min <= peak <= max");
    for (const auto& p : std::array<std::array<f32, 4>, 5>{{{0.f, 1.f, 0.f, 1.f}, {1.f, -1.f, 0.f, 1.f}, {1.f, 1.f, 1.f, 1.f},
                                                            {inf, 1.f, 0.f, 1.f}, {1.f, 1.f, 0.f, nan}}})
        expectProgTrap(Prog{}.F(p[0]).F(p[1]).F(p[2]).F(p[3]).emit(kBeta), "beta needs finite alpha > 0, beta > 0");
    const auto draw = [](f32 lo, f32 hi, std::array<f32, 3> triangle) {
        return Prog{}.F(lo).F(hi).F(triangle[0]).F(triangle[1]).F(triangle[2]).emit(kTriangular).emit(kFloatWith);
    };
    expectProgTrap(draw(2.f, 1.f, {0.f, 1.f, 4.f}), "lower bound exceeds the upper bound");
    expectProgTrap(draw(nan, 1.f, {0.f, 1.f, 4.f}), "random range bounds must be finite");
    expectProgTrap(draw(5.f, 5.f, {0.f, 1.f, 4.f}), "random range lies outside the distribution");
    expectProgTrap(draw(5.f, 9.f, {0.f, 1.f, 4.f}), "random range has no probability under the distribution");
    expectProgTrap(draw(4.f, 9.f, {0.f, 1.f, 4.f}), "random range has no probability under the distribution");
    expectProgTrap(Prog{}.I(0).I(10).F(1000.f).F(1.f).emit(kGaussian).emit(kIntWith),
                   "random range has no probability under the distribution");
    expectProgTrap(Prog{}.I(3).I(1).F(0.f).F(1.f).emit(kGaussian).emit(kIntWith), "lower bound exceeds the upper bound");
    // An equal range on the boundary of the support is valid.
    CHECK(test_support::evalProgF32(draw(4.f, 4.f, {0.f, 1.f, 4.f})) == 4.f);
}

TEST_CASE("truncated draws follow the distribution, not a clamped one") {
    const auto sample = [](const std::string& setup, const std::string& expression) {
        return xs(generate(compileSource(drawProgram(setup, expression, 4000), true), 11));
    };
    // p = 0.001 Kolmogorov-Smirnov bound for 4000 draws.
    constexpr double bound = 1.95 / 63.245553203367585;
    const auto gaussian = sample("distribution d = gaussian(mean=0.0, sigma=2.0)", "-5.0 to 5.0 with d");
    const double g0 = normal(-2.5), g1 = normal(2.5);
    CHECK(kolmogorov(gaussian, [&](double x) { return (normal(x / 2.0) - g0) / (g1 - g0); }) < bound);
    CHECK(*std::min_element(gaussian.begin(), gaussian.end()) >= -5.f);
    CHECK(std::count(gaussian.begin(), gaussian.end(), 5.f) < 3); // no clamping pile-up at the bounds
    const auto offCenter = sample("d = gaussian(mean=4.0, sigma=1.0)", "0.0 to 3.0 with d");
    const double o0 = normal(-4.0), o1 = normal(-1.0);
    CHECK(kolmogorov(offCenter, [&](double x) { return (normal(x - 4.0) - o0) / (o1 - o0); }) < bound);
    const auto triangle = sample("d = triangular(min=0.0, peak=0.0, max=4.0)", "0.0 to 4.0 with d");
    CHECK(kolmogorov(triangle, [](double x) { return 1.0 - (4.0 - x) * (4.0 - x) / 16.0; }) < bound);
    const auto inside = sample("d = triangular(min=-5.0, peak=1.0, max=5.0)", "-2.0 to 3.0 with d");
    const auto tri = [](double x) { return x < 1.0 ? (x + 5.0) * (x + 5.0) / 60.0 : 1.0 - (5.0 - x) * (5.0 - x) / 40.0; };
    CHECK(kolmogorov(inside, [&](double x) { return (tri(x) - tri(-2.0)) / (tri(3.0) - tri(-2.0)); }) < bound);
    // I_x(2, 5) = sum_{j=2..6} C(6, j) x^j (1 - x)^(6 - j)
    const auto beta25 = [](double x) {
        constexpr double c[] = {1, 6, 15, 20, 15, 6, 1};
        double total = 0.0;
        for (int j = 2; j <= 6; ++j) total += c[j] * std::pow(x, j) * std::pow(1.0 - x, 6 - j);
        return total;
    };
    const auto clean = sample("d = beta(alpha=2.0, beta=5.0)", "0.0 to 1.0 with d");
    CHECK(kolmogorov(clean, beta25) < bound);
    const auto scaled = sample("d = beta(alpha=2.0, beta=5.0, min=1.6, max=2.0)", "1.6 to 2.0 with d");
    CHECK(kolmogorov(scaled, [&](double x) { return beta25((x - 1.6) / 0.4); }) < bound);
    const auto edges = sample("d = beta(alpha=0.5, beta=0.5)", "0.0 to 1.0 with d");
    CHECK(kolmogorov(edges, [](double x) { return 2.0 / 3.141592653589793 * std::asin(std::sqrt(x)); }) < bound);
    const auto mirrored = sample("d = beta(alpha=5.0, beta=2.0)", "0.0 to 1.0 with d");
    CHECK(kolmogorov(mirrored, [&](double x) { return 1.0 - beta25(1.0 - x); }) < bound);
}

TEST_CASE("integer draws round the continuous draw half away from zero") {
    const auto values = xs(generate(compileSource(drawProgram("d = gaussian(mean=0.0, sigma=2.0)", "-5 to 5 with d", 11000), true), 3));
    std::array<int, 11> counts{};
    for (const f32 v : values) {
        REQUIRE(v == std::round(v));
        ++counts.at(static_cast<std::size_t>(static_cast<int>(v) + 5));
    }
    const double total = normal(2.5) - normal(-2.5);
    double chi = 0.0;
    for (int k = -5; k <= 5; ++k) {
        const double lo = std::max(-5.0, k - 0.5), hi = std::min(5.0, k + 0.5);
        const double expected = values.size() * (normal(hi / 2.0) - normal(lo / 2.0)) / total;
        chi += (counts[k + 5] - expected) * (counts[k + 5] - expected) / expected;
    }
    CHECK(chi < 29.59); // p = 0.001, 10 degrees of freedom
    // beta(1, 1) is uniform before rounding: the end values cover half intervals.
    const auto flat = xs(generate(compileSource(drawProgram("d = beta(alpha=1.0, beta=1.0, min=0.0, max=4.0)", "0 to 4 with d", 8000), true), 5));
    const auto ends = std::count(flat.begin(), flat.end(), 0.f) + std::count(flat.begin(), flat.end(), 4.f);
    const auto middle = std::count(flat.begin(), flat.end(), 2.f);
    CHECK(std::abs(static_cast<double>(ends) - static_cast<double>(middle)) < 0.15 * middle);
}

TEST_CASE("distributions are copied values with runtime parameters") {
    const std::string source = R"(asset t { input i32 a = 0 input i32 v = 0
  size 1x1
  input f32 spread = 0.8
  output list point p
  make(level: f32) -> distribution { return gaussian(mean=level, sigma=spread_of(level)) }
  spread_of(level: f32) -> f32 { return 0.1 + level * 0.0 }
  jitter(d: distribution) -> i32 { return -5 to 5 with d }
  draw {
    center = 8.0 + float(v)
    distribution shape = gaussian(mean=center, sigma=spread)
    center = 100.0
    copy = shape
    shape = triangular(min=0.0, peak=1.0, max=2.0)
    p = {5.0 to 12.0 with copy, 0.0 to 2.0 with shape}
    distribution random = gaussian(mean=0.0, sigma=(0.5 to 1.5))
    p = {jitter(random), jitter(make(float(a)))}
    p = {-5 to 5 with make(0.0), 0 to 0 with gaussian(mean=0.0, sigma=1.0)}
    p = {jitter(pick(copy, random)), 0}
  }
})";
    for (const bool optimize : {false, true}) {
        const auto bytes = compileSource(source, optimize);
        CHECK(bytes[6] == 0);
        for (u32 variant = 0; variant < 3; ++variant) {
            const auto result = generate(bytes, 9, variant);
            const auto& points = result.outputs[0].points;
            REQUIRE(points.size() == 4);
            // The copy kept the mean 8 + v although `center` and `shape` changed later.
            CHECK(points[0].x >= 5.f);
            CHECK(points[0].x <= 12.f);
            CHECK(std::fabs(points[0].x - (8.f + static_cast<f32>(variant))) < 4.f);
            CHECK(points[0].y >= 0.f);
            CHECK(points[0].y <= 2.f);
            CHECK(points[1].y == 0.f); // sigma 0.1 around 0
            CHECK(points[2].x == 0.f);
            CHECK(points[2].y == 0.f);
        }
        CHECK(pointBits(generate(bytes, 9, 1)) == pointBits(generate(compileSource(source, !optimize), 9, 1)));
    }
    // Two draws with one description are two independent draws.
    const auto twice = xs(generate(compileSource(drawProgram("d = gaussian(mean=0.0, sigma=3.0)", "-9 to 9 with d", 64), true), 2));
    CHECK(std::adjacent_find(twice.begin(), twice.end(), std::not_equal_to<>()) != twice.end());
}

TEST_CASE("bounds and then the distribution evaluate once before one draw") {
    const auto program = [](const std::string& body) {
        return "asset t { size 4x1 output list point p\n"
               "  f(x: i32) -> i32 { pixel(x, 0, #fff) return x + index(10) }\n"
               "  shape(s: f32) -> distribution { pixel(3, 0, #f00) return gaussian(mean=0.0, sigma=s + unit()) }\n"
               "  draw { " + body + " p = {index(1000), unit()} } }";
    };
    const std::vector<std::pair<std::string, std::string>> equivalents = {
        {"x = f(0) to f(1) + 20 with shape(2.0) p = {x, 0}",
         "l = f(0) u = f(1) + 20 d = shape(2.0) x = l to u with d p = {x, 0}"},
        {"d = gaussian(mean=0.0, sigma=0.5 to 1.5) p = {-3 to 3 with d, 0}",
         "s = 0.5 to 1.5 d = gaussian(mean=0.0, sigma=s) p = {-3 to 3 with d, 0}"},
        // A constructor draws nothing itself.
        {"d = gaussian(mean=0.0, sigma=1.0) p = {0, 0}", "p = {0, 0}"},
        // Equal bounds still take one step.
        {"x = 2 to 2 with shape(1.0) p = {x, 0}", "d = shape(1.0) x = 2 to 2 p = {x, 0}"},
    };
    for (const auto& [direct, stored] : equivalents) {
        INFO(direct);
        for (const bool optimize : {false, true})
            for (const u32 seed : {0u, 7u, 26u})
                CHECK(pointBits(generate(compileSource(program(direct), optimize), seed)) ==
                      pointBits(generate(compileSource(program(stored), false), seed)));
    }
}

TEST_CASE("invalid dynamic parameters and unassigned distributions trap, even unused") {
    const std::string unused = "asset t { input i32 v = 0  size 1x1 draw { dead = gaussian(mean=0.0, sigma=float(v) - 0.5) } }";
    const std::string unset = "asset t { input i32 v = 0  size 1x1 output point p draw {\n  if (v == 1) { distribution d = gaussian(mean=0.0, sigma=1.0) }\n  p = {0 to 5 with d, 0} } }";
    for (const bool optimize : {false, true}) {
        CHECK_THROWS_AS(generate(compileSource(unused, optimize), 0, 0), Trap);
        CHECK_NOTHROW(generate(compileSource(unused, optimize), 0, 1) );
        CHECK_THROWS_AS(generate(compileSource(unset, optimize), 0, 0), Trap);
        CHECK_NOTHROW(generate(compileSource(unset, optimize), 0, 1));
    }
    try {
        generate(compileSource(unset, false), 0, 0);
    } catch (const Trap& trap) {
        CHECK(std::string(trap.what()).find("distribution is not initialized") != std::string::npos);
    }
}

TEST_CASE("distribution types are checked in source") {
    const auto wrap = [](const std::string& body) { return "asset t { size 1x1 output point p draw { " + body + " } }"; };
    CHECK(sourceError(wrap("x = 0 to 5 with 2.0")) == "'with' needs a distribution, got f32");
    CHECK(sourceError(wrap("distribution d = 2.0")) == "distribution 'd' needs a distribution, got f32");
    CHECK(sourceError(wrap("d = gaussian(mean=0.0, sigma=1.0) d = 1.0")) == "local 'd' is distribution, cannot assign f32");
    CHECK(sourceError(wrap("d = gaussian(mean=0.0, sigma=1.0) x = d + 1")) ==
          "arithmetic requires numeric operands, got distribution and i32");
    CHECK(sourceError(wrap("d = gaussian(mean=0.0, sigma=1.0) e = d x = d == e")).find("distribution") != std::string::npos);
    CHECK(sourceError(wrap("d = gaussian(mean=0.0, sigma=1.0) p = d")).find("distribution") != std::string::npos);
    CHECK(sourceError(wrap("d = gaussian(mean=0.0, sigma=1.0) x = float(d)")).find("distribution") != std::string::npos);
    CHECK(sourceError(wrap("d = gaussian(mean=0.0, sigma=1.0) x = d.sigma")) == "distribution has no components, got '.sigma'");
    CHECK(sourceError(wrap("d = gaussian(mean=#fff, sigma=1.0)")).find("must be f32, got color") != std::string::npos);
    CHECK(sourceError(wrap("d = beta(alpha=1.0, beta=1.0, min=0.0)")).find("beta") != std::string::npos);
    CHECK(sourceError("asset t { size 1x1 input distribution d = 1 draw { } }").find("input type") != std::string::npos);
}

TEST_CASE("'with' parses after a range and formats alongside ordinary constructor helpers") {
    CHECK(format("asset t { size 1x1 draw { d = gaussian(mean=0.0, sigma=1.0) x = 1 + 2 to 9 with d < 5 } }")
              .find("x = (((1 + 2) to 9 with d) < 5)") != std::string::npos);
    const auto typed = format("asset t { size 1x1 draw { distribution d=gaussian(sigma=2.0,mean=0.0) x=-5 to 5 with d } }");
    CHECK(typed.find("distribution d = gaussian(mean = 0.0, sigma = 2.0)") != std::string::npos);
    CHECK(typed.find("x = ((-5) to 5 with d)") != std::string::npos);
    CHECK(format(typed) == typed);
    const auto helper = format("asset t { size 1x1 f(d: distribution) -> distribution { return d } draw { } }");
    CHECK(helper.find("f(d: distribution) -> distribution {") != std::string::npos);
    // Constructor builtins remain ordinary callable names; range/type keywords
    // cannot name locals or helpers.
    const std::string old = R"(asset t { size 4x1 output list point p
      gaussian(a: i32, b: i32) -> i32 { return a * 10 + b }
      beta(x: f32) -> f32 { return x * 2.0 }
      mark(x: i32) { pixel(x, 0, #fff) }
      draw {
        shapeCount = 3 markCount = 4
        p = {shapeCount, markCount}
        x = 1 mark(2)
        p = {gaussian(1, 2), beta(0.25)}
        y = (1 to 3 with triangular(min=1.0, peak=2.0, max=3.0)) p = {x, y}
      } })";
    for (const bool optimize : {false, true}) {
        const auto result = generate(compileSource(old, optimize), 0);
        const auto& points = result.outputs[0].points;
        REQUIRE(points.size() == 3);
        CHECK(points[0].x == 3.f);
        CHECK(points[0].y == 4.f);
        CHECK(points[1].x == 12.f);
        CHECK(points[1].y == 0.5f);
        CHECK(points[2].x == 1.f);
        CHECK(points[2].y >= 1.f);
        CHECK(points[2].y <= 3.f);
        CHECK(result.image.at(2, 0) == Color{255, 255, 255, 255});
    }
    const auto formatted = format(old);
    CHECK(format(formatted) == formatted);
    CHECK(stripDebug(compileSource(formatted, false)) == stripDebug(compileSource(old, false)));
}

TEST_CASE("files with distribution slots load like any other file") {
    for (const auto* source : {"asset t { size 1x1 f(d: distribution) -> i32 { return 1 } draw { } }",
                               "asset t { size 1x1 draw { pixel(0, 0, #fff) } }"}) {
        const auto bytes = compileSource(source, false);
        CHECK(bytes[6] == 0);
        CHECK_NOTHROW(validate(loadFile(bytes)));
    }
}
