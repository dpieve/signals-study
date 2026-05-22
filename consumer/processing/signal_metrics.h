#pragma once
#include <span>
#include <utility>
#include <cstddef>

/// Pure, stateless signal-metric functions.
///
/// All functions are noexcept and operate on a read-only span of double
/// samples.  They return sensible defaults (0.0) on empty input.

/// Simple sliding-window average over the last @p window samples.
/// If @p window is 0 or the span is empty the result is 0.0.
double moving_average(std::span<const double> samples, std::size_t window) noexcept;

/// Root-mean-square: sqrt( sum(x²) / n ).
double rms(std::span<const double> samples) noexcept;

/// Single-pass minimum and maximum.
/// Returns {0.0, 0.0} on empty input.
std::pair<double, double> min_max(std::span<const double> samples) noexcept;

/// Proxy for high-frequency noise: standard deviation of first-differences.
/// noise_level([a,b,c,...]) = std_dev([b-a, c-b, ...])
double noise_level(std::span<const double> samples) noexcept;

/// Signal quality in [0, 1].
/// quality = 1.0 - clamp(noise_level / rms, 0, 1)
/// Returns 0.0 when rms is near zero to avoid division-by-zero.
double signal_quality(std::span<const double> samples) noexcept;
