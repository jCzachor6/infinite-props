#include "iprop/math/Distribution.hpp"
#include <cmath>

namespace iprop::math {
const PointDistributionSpec* pointDistributionSpec(DistributionFamily family) {
    for (const auto& spec : kPointDistributionSpecs) if (spec.family == family) return &spec;
    return nullptr;
}
const PointDistributionSpec* pointDistributionSpec(std::string_view name) {
    for (const auto& spec : kPointDistributionSpecs) if (spec.name == name) return &spec;
    return nullptr;
}
const char* pointDistributionCountError(DistributionFamily family, std::size_t count) {
    const auto* spec = pointDistributionSpec(family);
    if (!spec) return "unknown point distribution family";
    if (count < spec->minimumPoints || count > kMaxDistributionPoints) return family == DistributionFamily::Density ?
        "density needs between 2 and 256 points" : "weighted needs between 1 and 256 points";
    return nullptr;
}
double distributionNumber(const DistributionNumber& number) {
    return std::visit([](auto value) { return static_cast<double>(value); }, number);
}

const char* pointDistributionEntryError(const DistributionPoint& point, const DistributionPoint* previous,
                                        DistributionFamily family) {
    const double key = distributionNumber(point.key), value = distributionNumber(point.value);
    if (!std::isfinite(key) || !std::isfinite(value)) return "distribution points must be finite";
    if (previous && !(distributionNumber(previous->key) < key)) return "distribution keys must be strictly increasing";
    if (value < 0.0) return family == DistributionFamily::Density ? "distribution densities must be nonnegative" :
        "distribution weights must be nonnegative";
    return nullptr;
}

double pointDistributionIncrement(DistributionFamily family, const DistributionPoint& point,
                                  const DistributionPoint* previous) {
    const auto value = distributionNumber(point.value);
    if (family == DistributionFamily::Weighted) return value;
    if (!previous) return 0.0;
    const double length = distributionNumber(point.key) - distributionNumber(previous->key);
    const double sum = distributionNumber(previous->value) + value;
    const double average = sum * 0.5;
    return length * average;
}

const char* pointDistributionError(const PointDistributionData& data, bool complete) {
    if (!pointDistributionSpec(data.family)) return "unknown point distribution family";
    if (complete) if (const auto* error = pointDistributionCountError(data.family, data.points.size())) return error;
    if (data.points.size() > kMaxDistributionPoints) return "distribution exceeds 256 points";
    double total = 0.0;
    for (std::size_t i = 0; i < data.points.size(); ++i) {
        const auto* previous = i ? &data.points[i - 1] : nullptr;
        if (const auto* error = pointDistributionEntryError(data.points[i], previous, data.family)) return error;
        total = total + pointDistributionIncrement(data.family, data.points[i], previous);
    }
    if (complete && !(total > 0.0)) return data.family == DistributionFamily::Density ?
        "density needs positive total area" : "weighted needs positive total mass";
    return nullptr;
}

u32 pointDrawWork(const Distribution& distribution, double lo, double hi) {
    if (!distribution.points) return 0;
    if (distribution.family == DistributionFamily::Density)
        return lo == hi ? 0 : static_cast<u32>(2 * (distribution.points->points.size() - 1));
    return static_cast<u32>(2 * distribution.points->points.size());
}

TruncatedDraw pointDistributionDraw(const Distribution& distribution, double lo, double hi, double u) {
    if (distribution.family == DistributionFamily::Density) return densityDistributionDraw(distribution, lo, hi, u);
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
            if (u == 1.0) { result.value = key; selected = true; }
            if (!selected && target < cumulative) { result.value = key; selected = true; }
        }
    }
    if (!(mass > 0.0)) result.status = DrawStatus::NoMass;
    else if (!selected) result.status = DrawStatus::Numerical;
    return result;
}
} // namespace iprop::math
