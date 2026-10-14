#include "iprop/math/Distribution.hpp"

#include <cmath>
#include <limits>

// Every expression keeps the operation order of spec v0.5 §4.5; the build
// disables floating contraction, so no step is fused or reassociated.
namespace iprop::math {
namespace {

struct NoConvergence {};

constexpr double kLn2Hi = 6.93147180369123816490e-01;
constexpr double kLn2Lo = 1.90821492927058770002e-10;
constexpr double kInvLn2 = 1.4426950408889634;
constexpr double kSqrt1_2 = 0.7071067811865476;
constexpr double kTwoOverSqrtPi = 1.1283791670955126;
constexpr double kInvSqrtPi = 0.5641895835477563;
constexpr double kHalfLog2Pi = 0.9189385332046728;
constexpr double kEpsilon = 2.220446049250313e-16;     // 2^-52
constexpr double kHalfEpsilon = 1.1102230246251565e-16; // 2^-53
constexpr double kTiny = 1e-300;
constexpr double kMinimumMass = 0x1p-1000;
constexpr double kSolverTolerance = 0x1p-30;
constexpr double kSolverStep = 0x1p-50;
constexpr double kInfinity = std::numeric_limits<double>::infinity();

// exp(-z * z / 2) with z * z split so its large part is exact (Cody).
double gaussExp(double z) {
    const double a = std::fabs(z);
    const double h = std::trunc(a * 16.0) / 16.0;
    return pinnedExp(-(h * h) * 0.5) * pinnedExp(-((a - h) * (a + h)) * 0.5);
}

constexpr double kLanczos[9] = {0.99999999999980993, 676.5203681218851, -1259.1392167224028,
    771.32342877765313, -176.61502916214059, 12.507343278686905, -0.13857109526572012,
    9.9843695780195716e-6, 1.5056327351493116e-7};

// The incomplete beta continued fraction (modified Lentz).
double betaFraction(double a, double b, double x, u32& steps) {
    const double qab = a + b, qap = a + 1.0, qam = a - 1.0;
    double c = 1.0;
    double d = 1.0 - qab * x / qap;
    if (std::fabs(d) < kTiny) d = kTiny;
    d = 1.0 / d;
    double h = d;
    for (int m = 1;; ++m) {
        ++steps;
        const double m2 = 2.0 * m;
        double aa = m * (b - m) * x / ((qam + m2) * (a + m2));
        d = 1.0 + aa * d;
        if (std::fabs(d) < kTiny) d = kTiny;
        c = 1.0 + aa / c;
        if (std::fabs(c) < kTiny) c = kTiny;
        d = 1.0 / d;
        h = h * (d * c);
        aa = -(a + m) * (qab + m) * x / ((a + m2) * (qap + m2));
        d = 1.0 + aa * d;
        if (std::fabs(d) < kTiny) d = kTiny;
        c = 1.0 + aa / c;
        if (std::fabs(c) < kTiny) c = kTiny;
        d = 1.0 / d;
        const double delta = d * c;
        h = h * delta;
        if (std::fabs(delta - 1.0) <= kEpsilon) return h;
        if (m == 300) throw NoConvergence{};
    }
}

double logBeta(double a, double b) { return logGamma(a) + logGamma(b) - logGamma(a + b); }

// log I_t(a, b) for 0 < t < 1, without underflow in the lower tail.
double logIncompleteBeta(double a, double b, double t, double lb, u32& steps) {
    const double front = a * pinnedLog(t) + b * pinnedLog(1.0 - t) - lb;
    if (t < (a + 1.0) / (a + b + 2.0)) return front + pinnedLog(betaFraction(a, b, t, steps) / a);
    return pinnedLog(1.0 - pinnedExp(front) * betaFraction(b, a, 1.0 - t, steps) / b);
}

// I_t(a, b) = p for t in [lo, hi]: Newton on log I over log t, falling back
// to geometric or linear bisection.
double betaQuantile(double a, double b, double p, double lo, double hi, u32& steps) {
    const double lb = logBeta(a, b);
    const double lp = pinnedLog(p);
    const double b1 = b - 1.0;
    double t = 0.5 * (lo + hi);
    for (int it = 1;; ++it) {
        ++steps;
        const double h = logIncompleteBeta(a, b, t, lb, steps) - lp;
        if (std::fabs(h) <= kSolverTolerance) return t;
        if (h < 0.0) lo = t;
        else hi = t;
        double next = lo == 0.0 ? hi * 0x1p-64 : hi > 2.0 * lo ? std::sqrt(lo) * std::sqrt(hi) : 0.5 * (lo + hi);
        const double ratio = pinnedExp((h + lp) - (a * pinnedLog(t) + b1 * pinnedLog(1.0 - t) - lb));
        const double candidate = t * pinnedExp(-h * ratio);
        if (lo < candidate && candidate < hi) next = candidate;
        if (next <= lo) return lo;
        if (next >= hi) return hi;
        if (std::fabs(next - t) <= kSolverStep * next || hi - lo <= kSolverStep * hi) return next;
        t = next;
        if (it == 64) throw NoConvergence{};
    }
}

// The family in binary64, with its mirror image about zero: P(X >= x) of a
// family is P(-X <= -x) of its mirror (spec v0.5 §4.3).
struct Family {
    DistributionFamily kind;
    double p0, p1, p2, p3;
};

Family family(const Distribution& d) {
    const auto& p = d.parameters;
    return {d.family, p[0], p[1], p[2], p[3]};
}

Family mirror(const Family& f) {
    switch (f.kind) {
    case DistributionFamily::Gaussian: return {f.kind, -f.p0, f.p1, 0.0, 0.0};
    case DistributionFamily::Triangular: return {f.kind, -f.p2, -f.p1, -f.p0, 0.0};
    default: return {f.kind, f.p1, f.p0, -f.p3, -f.p2};
    }
}

double lowerSupport(const Family& f) {
    return f.kind == DistributionFamily::Gaussian ? -kInfinity
         : f.kind == DistributionFamily::Triangular ? f.p0 : f.p2;
}

double upperSupport(const Family& f) {
    return f.kind == DistributionFamily::Gaussian ? kInfinity
         : f.kind == DistributionFamily::Triangular ? f.p2 : f.p3;
}

double cdf(const Family& f, double x, u32& steps) {
    switch (f.kind) {
    case DistributionFamily::Gaussian: return normalCdf((x - f.p0) / f.p1, steps);
    case DistributionFamily::Triangular: {
        const double a = f.p0, c = f.p1, b = f.p2;
        if (x <= a) return 0.0;
        if (x >= b) return 1.0;
        if (x < c) return (x - a) * (x - a) / ((b - a) * (c - a));
        return 1.0 - (b - x) * (b - x) / ((b - a) * (b - c));
    }
    default: return incompleteBeta(f.p0, f.p1, (x - f.p2) / (f.p3 - f.p2), steps);
    }
}

// x in [lower, upper] with F(x) = p, for p <= 1/2.
double quantile(const Family& f, double p, double lower, double upper, u32& steps) {
    switch (f.kind) {
    case DistributionFamily::Gaussian: return f.p0 + f.p1 * normalQuantile(p);
    case DistributionFamily::Triangular: {
        const double a = f.p0, c = f.p1, b = f.p2;
        if (p < (c - a) / (b - a)) return a + std::sqrt(p * (b - a) * (c - a));
        return b - std::sqrt((1.0 - p) * (b - a) * (b - c));
    }
    default: {
        const double w = f.p3 - f.p2;
        const double t = betaQuantile(f.p0, f.p1, p, (lower - f.p2) / w, (upper - f.p2) / w, steps);
        return f.p2 + t * w;
    }
    }
}

} // namespace

double pinnedExp(double x) {
    if (x > 709.782712893384) return kInfinity;
    if (x < -745.2) return 0.0;
    const double k = std::floor(x * kInvLn2 + 0.5);
    const double r = (x - k * kLn2Hi) - k * kLn2Lo;
    double e = 1.0;
    for (int n = 13; n > 0; --n) e = 1.0 + r * e / n;
    return std::ldexp(e, static_cast<int>(k));
}

double pinnedLog(double x) {
    int e = 0;
    double m = std::frexp(x, &e);
    if (m < kSqrt1_2) {
        m = m * 2.0;
        e = e - 1;
    }
    const double s = (m - 1.0) / (m + 1.0);
    const double s2 = s * s;
    double q = 1.0 / 25.0;
    for (int k = 11; k >= 0; --k) q = q * s2 + 1.0 / (2 * k + 1);
    return e * kLn2Hi + (2.0 * s * q + e * kLn2Lo);
}

double normalCdf(double z, u32& steps) {
    const double x = std::fabs(z) * kSqrt1_2;
    if (x < 1.5) {
        // erf(x) = 2/sqrt(pi) exp(-x^2) sum (2x^2)^n x / (2n+1)!!
        const double x2 = x * x;
        double term = x;
        double total = x;
        for (int n = 1;; ++n) {
            ++steps;
            term = term * (2.0 * x2) / (2 * n + 1);
            total = total + term;
            if (term <= total * kHalfEpsilon) break;
            if (n == 100) throw NoConvergence{};
        }
        const double erf = kTwoOverSqrtPi * gaussExp(z) * total;
        return z < 0.0 ? 0.5 * (1.0 - erf) : 0.5 * (1.0 + erf);
    }
    // erfc(x) = exp(-x^2) / (sqrt(pi) K), K = x + (1/2)/(x + 1/(x + (3/2)/(x + ...)))
    double f = x, c = x, d = 0.0;
    for (int n = 1;; ++n) {
        ++steps;
        const double a = n * 0.5;
        d = x + a * d;
        if (d == 0.0) d = kTiny;
        c = x + a / c;
        if (c == 0.0) c = kTiny;
        d = 1.0 / d;
        const double delta = c * d;
        f = f * delta;
        if (std::fabs(delta - 1.0) <= kEpsilon) break;
        if (n == 256) throw NoConvergence{};
    }
    const double erfc = gaussExp(z) * kInvSqrtPi / f;
    return z < 0.0 ? 0.5 * erfc : 1.0 - 0.5 * erfc;
}

// Wichura's AS241 (PPND16) for 0 < p < 1.
double normalQuantile(double p) {
    const double q = p - 0.5;
    if (std::fabs(q) <= 0.425) {
        const double r = 0.180625 - q * q;
        const double num = (((((((2.5090809287301226727e+3 * r + 3.3430575583588128105e+4) * r +
            6.7265770927008700853e+4) * r + 4.5921953931549871457e+4) * r + 1.3731693765509461125e+4) * r +
            1.9715909503065514427e+3) * r + 1.3314166789178437745e+2) * r + 3.3871328727963666080e+0) * q;
        const double den = (((((((5.2264952788528545610e+3 * r + 2.8729085735721942674e+4) * r +
            3.9307895800092710610e+4) * r + 2.1213794301586595867e+4) * r + 5.3941960214247511077e+3) * r +
            6.8718700749205790830e+2) * r + 4.2313330701600911252e+1) * r + 1.0);
        return num / den;
    }
    double r = q <= 0.0 ? p : 1.0 - p;
    r = std::sqrt(-pinnedLog(r));
    double num = 0.0, den = 0.0;
    if (r <= 5.0) {
        r = r - 1.6;
        num = (((((((7.74545014278341407640e-4 * r + 2.27238449892691845833e-2) * r +
            2.41780725177450611770e-1) * r + 1.27045825245236838258e+0) * r + 3.64784832476320460504e+0) * r +
            5.76949722146069140550e+0) * r + 4.63033784615654529590e+0) * r + 1.42343711074968357734e+0);
        den = (((((((1.05075007164441684324e-9 * r + 5.47593808499534494600e-4) * r +
            1.51986665636164571966e-2) * r + 1.48103976427480074590e-1) * r + 6.89767334985100004550e-1) * r +
            1.67638483018380384940e+0) * r + 2.05319162663775882187e+0) * r + 1.0);
    } else {
        r = r - 5.0;
        num = (((((((2.01033439929228813265e-7 * r + 2.71155556874348757815e-5) * r +
            1.24266094738807843860e-3) * r + 2.65321895265761230930e-2) * r + 2.96560571828504891230e-1) * r +
            1.78482653991729133580e+0) * r + 5.46378491116411436990e+0) * r + 6.65790464350110377720e+0);
        den = (((((((2.04426310338993978564e-15 * r + 1.42151175831644588870e-7) * r +
            1.84631831751005468180e-5) * r + 7.86869131145613259100e-4) * r + 1.48753612908506148525e-2) * r +
            1.36929880922735805310e-1) * r + 5.99832206555887937690e-1) * r + 1.0);
    }
    const double x = num / den;
    return q < 0.0 ? -x : x;
}

// Lanczos (g = 7, n = 9); below 1/2 via lgamma(x) = lgamma(x + 1) - log(x).
double logGamma(double x) {
    if (x < 0.5) return logGamma(x + 1.0) - pinnedLog(x);
    x = x - 1.0;
    double a = kLanczos[0];
    for (int i = 1; i < 9; ++i) a = a + kLanczos[i] / (x + i);
    const double t = x + 7.5;
    return (kHalfLog2Pi + (x + 0.5) * pinnedLog(t)) - t + pinnedLog(a);
}

double incompleteBeta(double a, double b, double t, u32& steps) {
    if (t <= 0.0) return 0.0;
    if (t >= 1.0) return 1.0;
    const double front = pinnedExp(a * pinnedLog(t) + b * pinnedLog(1.0 - t) - logBeta(a, b));
    if (t < (a + 1.0) / (a + b + 2.0)) return front * betaFraction(a, b, t, steps) / a;
    return 1.0 - front * betaFraction(b, a, 1.0 - t, steps) / b;
}

bool validDistribution(const Distribution& d) {
    const auto& p = d.parameters;
    const auto finite = [](f32 v) { return std::isfinite(v); };
    switch (d.family) {
    case DistributionFamily::Gaussian: return finite(p[0]) && finite(p[1]) && p[1] > 0.f;
    case DistributionFamily::Triangular:
        return finite(p[0]) && finite(p[1]) && finite(p[2]) && p[0] < p[2] && p[0] <= p[1] && p[1] <= p[2];
    case DistributionFamily::Beta:
        return finite(p[0]) && finite(p[1]) && p[0] > 0.f && p[1] > 0.f &&
               finite(p[2]) && finite(p[3]) && p[2] < p[3];
    case DistributionFamily::None: return false;
    case DistributionFamily::Weighted: case DistributionFamily::Density: return d.points && d.points->family == d.family;
    }
    return false;
}

i32 roundRangeInt(double x) {
    double t = std::trunc(x);
    const double fraction = x - t;
    if (fraction >= 0.5) t += 1.0;
    else if (fraction <= -0.5) t -= 1.0;
    return static_cast<i32>(t);
}

TruncatedDraw truncatedDraw(const Distribution& distribution, double lo, double hi, double u) {
    if (distribution.family == DistributionFamily::Weighted || distribution.family == DistributionFamily::Density)
        return pointDistributionDraw(distribution, lo, hi, u);
    TruncatedDraw result;
    const Family f = family(distribution);
    try {
        if (lo == hi) {
            if (lo < lowerSupport(f) || lo > upperSupport(f)) result.status = DrawStatus::Outside;
            result.value = lo;
            return result;
        }
        const double lower = lo > lowerSupport(f) ? lo : lowerSupport(f);
        const double upper = hi < upperSupport(f) ? hi : upperSupport(f);
        if (!(lower < upper)) {
            result.status = DrawStatus::NoMass;
            return result;
        }
        const Family m = mirror(f);
        const double below = cdf(f, lower, result.steps);  // P(X <= lower)
        const double above = cdf(m, -upper, result.steps); // P(X >= upper)
        double mass = 0.0;
        if (below >= 0.5) mass = cdf(m, -lower, result.steps) - above;
        else if (above >= 0.5) mass = cdf(f, upper, result.steps) - below;
        else mass = (1.0 - above) - below;
        if (!(mass >= kMinimumMass)) {
            result.status = DrawStatus::NoMass;
            return result;
        }
        // Closed quantiles for deterministic steppers. Existing random draws
        // always use 0 < u < 1 and retain their exact operation sequence.
        if (u == 0.0 || u == 1.0) {
            result.value = u == 0.0 ? lower : upper;
            return result;
        }
        const double p = below + u * mass;
        double x = p <= 0.5 ? quantile(f, p, lower, upper, result.steps)
                            : -quantile(m, above + (1.0 - u) * mass, -upper, -lower, result.steps);
        if (x < lower) x = lower;
        if (x > upper) x = upper;
        result.value = x;
    } catch (const NoConvergence&) {
        result.status = DrawStatus::NoConvergence;
    }
    return result;
}

} // namespace iprop::math
