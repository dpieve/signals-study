#pragma once
#include <expected>
#include <flat_map>
#include <string>
#include <variant>
#include <vector>
#include "../common/telemetry_types.h"

// ---------------------------------------------------------------------------
// Result types — one per metric
// ---------------------------------------------------------------------------

struct MovingAverageResult { ChannelId channel; double value; };
struct RmsResult            { ChannelId channel; double value; };
struct MinMaxResult         { ChannelId channel; double min_val; double max_val; };
struct NoiseResult          { ChannelId channel; double level; };
struct SignalQualityResult  { ChannelId channel; double quality; };

struct ProcessingError { std::string message; };

using ProcessingResult = std::variant<
    MovingAverageResult,
    RmsResult,
    MinMaxResult,
    NoiseResult,
    SignalQualityResult>;

// ---------------------------------------------------------------------------
// ProcessingEngine
// ---------------------------------------------------------------------------

/// Stateful signal-processing engine.
///
/// Accumulates per-channel sample history and computes all five metrics for
/// every channel present in a CommittedBatch.  The internal history grows
/// unboundedly in this implementation; callers should discard or compact the
/// engine periodically for long-running deployments.
class ProcessingEngine {
public:
    ProcessingEngine() = default;

    /// Process all packets in @p batch and return a flat list of results.
    /// One MovingAverageResult, RmsResult, MinMaxResult, NoiseResult, and
    /// SignalQualityResult is appended per channel.
    [[nodiscard]] std::expected<std::vector<ProcessingResult>, ProcessingError>
    process_batch(const CommittedBatch& batch);

private:
    // Per-channel accumulated double samples (extracted from GeneratorSample).
    std::flat_map<ChannelId, std::vector<double>> channel_samples_;

    static constexpr std::size_t kMovingAverageWindow = 64;
};
