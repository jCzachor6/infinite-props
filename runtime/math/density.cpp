#include "iprop/math/Distribution.hpp"
#include <array>
#include <cmath>

namespace iprop::math {
namespace {
struct Segment { double lower = 0, upper = 0, p = 0, q = 0, area = 0; };
// Convex interpolation avoids cancellation into negative endpoint heights.
// Every product/sum is a separate binary64 operation, without contraction.
double height(double key, double a, double b, double p, double q) {
    if (key == a) return p;
    if (key == b) return q;
    const double t = (key - a) / (b - a);
    const double left = p * (1.0 - t);
    const double right = q * t;
    return left + right;
}
Segment clip(const DistributionPoint& first, const DistributionPoint& last, double lo, double hi) {
    const double a = distributionNumber(first.key), b = distributionNumber(last.key);
    Segment segment;
    segment.lower = lo > a ? lo : a;
    segment.upper = hi < b ? hi : b;
    if (!(segment.lower < segment.upper)) return segment;
    const double p = distributionNumber(first.value), q = distributionNumber(last.value);
    segment.p = height(segment.lower, a, b, p, q);
    segment.q = height(segment.upper, a, b, p, q);
    const double length = segment.upper - segment.lower;
    const double sum = segment.p + segment.q;
    const double average = sum * 0.5;
    segment.area = length * average;
    return segment;
}
}

TruncatedDraw invertDensitySegment(double length, double p, double q, double z) {
    TruncatedDraw result;
    if (!(length > 0.0) || !std::isfinite(length) || !std::isfinite(p) || !std::isfinite(q) ||
        !std::isfinite(z) || p < 0.0 || q < 0.0 || z < 0.0) {
        result.status = DrawStatus::Numerical; return result;
    }
    if (z == 0.0) return result;
    const double difference = q - p;
    const double s = difference / length;
    double x;
    if (s == 0.0) x = z / p;
    else {
        const double A = p * p;
        const double twiceSlope = 2.0 * s;
        const double B = twiceSlope * z;
        double R = A + B;
        if (R < 0.0) {
            const double magnitudes = std::fabs(A) + std::fabs(B);
            const double tolerance = 0x1p-49 * magnitudes; // 8 * 2^-52
            if (-R > tolerance) { result.status = DrawStatus::Numerical; return result; }
            R = 0.0;
        }
        const double root = std::sqrt(R);
        const double numerator = 2.0 * z;
        const double denominator = p + root;
        x = numerator / denominator;
    }
    if (!std::isfinite(x)) { result.status = DrawStatus::Numerical; return result; }
    // Only correct the solution's endpoint rounding, after solving the integral.
    result.value = x < 0.0 ? 0.0 : x > length ? length : x;
    return result;
}

TruncatedDraw densityDistributionDraw(const Distribution& distribution, double lo, double hi, double u) {
    TruncatedDraw result;
    if (!distribution.points || distribution.points->points.size() < 2) {
        result.status = DrawStatus::NoMass; return result;
    }
    const auto& points = distribution.points->points;
    // The segment table is fixed; a larger point list is not a valid density.
    if (points.size() > kMaxDistributionPoints) {
        result.status = DrawStatus::Numerical; return result;
    }
    if (lo == hi) {
        if (lo < distributionNumber(points.front().key) || lo > distributionNumber(points.back().key))
            result.status = DrawStatus::Outside;
        result.value = lo; return result;
    }
    std::array<Segment, kMaxDistributionPoints - 1> segments;
    double total = 0.0;
    for (std::size_t i = 1; i < points.size(); ++i) {
        segments[i - 1] = clip(points[i - 1], points[i], lo, hi);
        total = total + segments[i - 1].area;
    }
    const double target = u * total;
    double cumulative = 0.0;
    bool selected = false;
    for (std::size_t i = 0; i + 1 < points.size(); ++i) {
        const auto& segment = segments[i];
        if (!(segment.area > 0.0)) continue;
        const double next = cumulative + segment.area;
        if (u == 1.0) { result.value = segment.upper; selected = true; }
        if (!selected && target < next) {
            const double z = target - cumulative;
            result = invertDensitySegment(segment.upper - segment.lower, segment.p, segment.q, z);
            if (result.status == DrawStatus::Ok) {
                double value = segment.lower + result.value;
                if (value < segment.lower) value = segment.lower;
                if (value > segment.upper) value = segment.upper;
                result.value = value;
            }
            selected = true;
        }
        cumulative = next;
    }
    if (!(total > 0.0)) result.status = DrawStatus::NoMass;
    else if (!selected) result.status = DrawStatus::Numerical;
    return result;
}
} // namespace iprop::math
