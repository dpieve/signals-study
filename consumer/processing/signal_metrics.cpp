#include "signal_metrics.h"
#include <cmath>
#include <algorithm>
#include <numeric>
#include <limits>

double moving_average(std::span<const double> samples, std::size_t window) noexcept {
    if (samples.empty() || window == 0) { return 0.0; }

    // Use only the last `window` samples (or fewer if the span is shorter).
    const std::size_t effective = std::min(window, samples.size());
    const std::size_t offset    = samples.size() - effective;

    double sum = 0.0;
    for (std::size_t i = offset; i < samples.size(); ++i) {
        sum += samples[i];
    }
    return sum / static_cast<double>(effective);
}

double rms(std::span<const double> samples) noexcept {
    if (samples.empty()) { return 0.0; }

    double sum_sq = 0.0;
    for (const double v : samples) {
        sum_sq += v * v;
    }
    return std::sqrt(sum_sq / static_cast<double>(samples.size()));
}

std::pair<double, double> min_max(std::span<const double> samples) noexcept {
    if (samples.empty()) { return {0.0, 0.0}; }

    double lo = samples[0];
    double hi = samples[0];
    for (std::size_t i = 1; i < samples.size(); ++i) {
        if (samples[i] < lo) { lo = samples[i]; }
        if (samples[i] > hi) { hi = samples[i]; }
    }
    return {lo, hi};
}

double noise_level(std::span<const double> samples) noexcept {
    if (samples.size() < 2) { return 0.0; }

    // Compute first differences.
    const std::size_t n = samples.size() - 1;

    // Mean of differences.
    double mean = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        mean += samples[i + 1] - samples[i];
    }
    mean /= static_cast<double>(n);

    // Variance of differences.
    double var = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        const double d = (samples[i + 1] - samples[i]) - mean;
        var += d * d;
    }
    var /= static_cast<double>(n);

    return std::sqrt(var);
}

double signal_quality(std::span<const double> samples) noexcept {
    if (samples.empty()) { return 0.0; }

    const double r = rms(samples);
    constexpr double kEpsilon = 1e-12;
    if (r < kEpsilon) { return 0.0; }

    const double noise = noise_level(samples);
    const double ratio = std::clamp(noise / r, 0.0, 1.0);
    return 1.0 - ratio;
}
