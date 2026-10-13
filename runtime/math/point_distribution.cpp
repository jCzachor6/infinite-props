#include "iprop/math/Distribution.hpp"
#include <cmath>

namespace iprop::math {
double distributionNumber(const DistributionNumber& number) {
    return std::visit([](auto value) { return static_cast<double>(value); }, number);
}

const char* pointDistributionEntryError(const DistributionPoint& point, const DistributionPoint* previous) {
    const double key = distributionNumber(point.key), value = distributionNumber(point.value);
    if (!std::isfinite(key) || !std::isfinite(value)) return "distribution points must be finite";
    if (previous && !(distributionNumber(previous->key) < key)) return "distribution keys must be strictly increasing";
    if (value < 0.0) return "distribution weights must be nonnegative";
    return nullptr;
}

const char* pointDistributionError(const PointDistributionData& data, bool complete) {
    if (data.family != DistributionFamily::Weighted) return "unknown point distribution family";
    if (data.points.size() > 256 || (complete && data.points.empty()))
        return "weighted needs between 1 and 256 points";
    double total = 0.0;
    for (std::size_t i = 0; i < data.points.size(); ++i) {
        if (const auto* error = pointDistributionEntryError(data.points[i], i ? &data.points[i - 1] : nullptr)) return error;
        const auto weight = distributionNumber(data.points[i].value);
        total = total + weight;
    }
    if (complete && !(total > 0.0)) return "weighted needs positive total mass";
    return nullptr;
}

u32 pointDrawWork(const Distribution& distribution) {
    return distribution.points ? static_cast<u32>(2 * distribution.points->points.size()) : 0;
}

TruncatedDraw pointDistributionDraw(const Distribution& distribution, double lo, double hi, double u) {
    TruncatedDraw result;
    if (!distribution.points) { result.status = DrawStatus::NoMass; return result; }
    const auto& points = distribution.points->points;
    double mass = 0.0;
    for (const auto& point : points) {
        const double key = distributionNumber(point.key);
        if (key >= lo && key <= hi) mass = mass + distributionNumber(point.value);
    }
    const double target = u * mass;
    double cumulative = 0.0;
    bool selected = false;
    // Scan every point in both passes. The pinned cost is two fuel per point,
    // reserved before the kernel runs, including zero/excluded points.
    for (const auto& point : points) {
        const double key = distributionNumber(point.key);
        const double weight = distributionNumber(point.value);
        if (key >= lo && key <= hi && weight > 0.0) {
            cumulative = cumulative + weight;
            if (!selected && target < cumulative) { result.value = key; selected = true; }
        }
    }
    if (!(mass > 0.0)) result.status = DrawStatus::NoMass;
    else if (!selected) result.status = DrawStatus::Numerical;
    return result;
}
} // namespace iprop::math
