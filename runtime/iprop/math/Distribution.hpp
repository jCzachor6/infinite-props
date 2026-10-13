#pragma once

#include "iprop/Types.hpp"

namespace iprop::math {

// Pinned binary64 kernels of the distributions (spec v0.5 §4.5). They use
// only correctly rounded IEEE operations, so native and WASM agree bit for
// bit; `steps` counts the iterations that cost fuel.
double pinnedExp(double x);
double pinnedLog(double x);
double normalCdf(double z, u32& steps);
double normalQuantile(double p);
double logGamma(double x);
double incompleteBeta(double a, double b, double t, u32& steps);

// Constructor parameter rules (spec v0.5 §4.2).
bool validDistribution(const Distribution& distribution);
double distributionNumber(const DistributionNumber& number);
// Shared constructor rules for source constants, file pools and runtime
// snapshots; null means valid. Prefix validation is used by the builder.
const char* pointDistributionError(const PointDistributionData& data, bool complete = true);
const char* pointDistributionEntryError(const DistributionPoint& point,
                                        const DistributionPoint* previous = nullptr);
u32 pointDrawWork(const Distribution& distribution);

enum class DrawStatus : u8 { Ok, Outside, NoMass, NoConvergence, Numerical };
struct TruncatedDraw {
    double value = 0.0;
    u32 steps = 0;
    DrawStatus status = DrawStatus::Ok;
};
TruncatedDraw pointDistributionDraw(const Distribution& distribution, double lo, double hi, double u);

// A draw from `distribution` truncated to [lo, hi] (finite, lo <= hi) for the
// unit coefficient u in (0, 1) (spec v0.5 §4.4).
TruncatedDraw truncatedDraw(const Distribution& distribution, double lo, double hi, double u);

} // namespace iprop::math
