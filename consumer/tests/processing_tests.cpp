#include <gtest/gtest.h>
#include <cmath>
#include "../processing/signal_metrics.h"
#include "../processing/processing_engine.h"
#include "../common/telemetry_types.h"

// ---------------------------------------------------------------------------
// signal_metrics — purely functional, no threads, no queues
// ---------------------------------------------------------------------------

TEST(SignalMetrics, RmsKnownValues) {
    // rms([0, 1, 0, -1]) = sqrt((0+1+0+1)/4) = sqrt(0.5) = 1/sqrt(2)
    const std::vector<double> samples = {0.0, 1.0, 0.0, -1.0};
    const double result = rms(std::span<const double>(samples));
    EXPECT_NEAR(result, 1.0 / std::sqrt(2.0), 1e-12);
}

TEST(SignalMetrics, RmsEmpty) {
    const std::vector<double> samples;
    EXPECT_EQ(rms(std::span<const double>(samples)), 0.0);
}

TEST(SignalMetrics, RmsConstant) {
    // rms([2, 2, 2]) = 2
    const std::vector<double> samples = {2.0, 2.0, 2.0};
    EXPECT_NEAR(rms(std::span<const double>(samples)), 2.0, 1e-12);
}

TEST(SignalMetrics, MinMaxKnownValues) {
    // min_max([1, 2, 3]) == (1, 3)
    const std::vector<double> samples = {1.0, 2.0, 3.0};
    auto [lo, hi] = min_max(std::span<const double>(samples));
    EXPECT_DOUBLE_EQ(lo, 1.0);
    EXPECT_DOUBLE_EQ(hi, 3.0);
}

TEST(SignalMetrics, MinMaxSingleElement) {
    const std::vector<double> samples = {42.0};
    auto [lo, hi] = min_max(std::span<const double>(samples));
    EXPECT_DOUBLE_EQ(lo, 42.0);
    EXPECT_DOUBLE_EQ(hi, 42.0);
}

TEST(SignalMetrics, MinMaxEmpty) {
    const std::vector<double> samples;
    auto [lo, hi] = min_max(std::span<const double>(samples));
    EXPECT_DOUBLE_EQ(lo, 0.0);
    EXPECT_DOUBLE_EQ(hi, 0.0);
}

TEST(SignalMetrics, MinMaxNegativeValues) {
    const std::vector<double> samples = {-5.0, -1.0, -3.0};
    auto [lo, hi] = min_max(std::span<const double>(samples));
    EXPECT_DOUBLE_EQ(lo, -5.0);
    EXPECT_DOUBLE_EQ(hi, -1.0);
}

TEST(SignalMetrics, MovingAverageWindow2) {
    // moving_average([1, 2, 3, 4], window=2) uses last 2 elements: (3+4)/2 = 3.5
    const std::vector<double> samples = {1.0, 2.0, 3.0, 4.0};
    EXPECT_NEAR(moving_average(std::span<const double>(samples), 2), 3.5, 1e-12);
}

TEST(SignalMetrics, MovingAverageWindowLargerThanSamples) {
    // window > size → use all samples
    const std::vector<double> samples = {2.0, 4.0};
    EXPECT_NEAR(moving_average(std::span<const double>(samples), 100), 3.0, 1e-12);
}

TEST(SignalMetrics, MovingAverageWindowZero) {
    const std::vector<double> samples = {1.0, 2.0, 3.0};
    EXPECT_EQ(moving_average(std::span<const double>(samples), 0), 0.0);
}

TEST(SignalMetrics, MovingAverageEmpty) {
    const std::vector<double> samples;
    EXPECT_EQ(moving_average(std::span<const double>(samples), 4), 0.0);
}

TEST(SignalMetrics, NoiseLevelConstantSignal) {
    // Constant signal → all differences are 0 → noise = 0
    const std::vector<double> samples = {3.0, 3.0, 3.0, 3.0};
    EXPECT_NEAR(noise_level(std::span<const double>(samples)), 0.0, 1e-12);
}

TEST(SignalMetrics, NoiseLevelAlternating) {
    // [1, -1, 1, -1] → diffs = [-2, 2, -2] → mean=0, var = (4+4+4)/3 = 4 → std=2
    const std::vector<double> samples = {1.0, -1.0, 1.0, -1.0};
    EXPECT_NEAR(noise_level(std::span<const double>(samples)), 2.0, 1e-10);
}

TEST(SignalMetrics, NoiseLevelEmpty) {
    const std::vector<double> samples;
    EXPECT_EQ(noise_level(std::span<const double>(samples)), 0.0);
}

TEST(SignalMetrics, NoiseLevelSingleSample) {
    const std::vector<double> samples = {5.0};
    EXPECT_EQ(noise_level(std::span<const double>(samples)), 0.0);
}

TEST(SignalMetrics, SignalQualityHighQuality) {
    // Pure DC signal → noise=0 → quality=1.0
    const std::vector<double> samples = {5.0, 5.0, 5.0, 5.0};
    EXPECT_NEAR(signal_quality(std::span<const double>(samples)), 1.0, 1e-12);
}

TEST(SignalMetrics, SignalQualityZeroRms) {
    // All zeros → rms ≈ 0 → quality = 0.0 (guard against division by zero)
    const std::vector<double> samples = {0.0, 0.0, 0.0};
    EXPECT_EQ(signal_quality(std::span<const double>(samples)), 0.0);
}

TEST(SignalMetrics, SignalQualityRange) {
    // Result must be in [0, 1]
    const std::vector<double> samples = {1.0, -1.0, 1.0, -1.0, 0.5, -0.5};
    const double q = signal_quality(std::span<const double>(samples));
    EXPECT_GE(q, 0.0);
    EXPECT_LE(q, 1.0);
}

// ---------------------------------------------------------------------------
// ProcessingEngine — no threads, driven by constructed CommittedBatch
// ---------------------------------------------------------------------------

static CommittedBatch make_batch(uint64_t seq,
                                 uint32_t channel_id,
                                 const std::vector<double>& values) {
    CommittedBatch batch;
    batch.commit_sequence = seq;

    IngestedPacket pkt{};
    pkt.sequence      = seq;
    pkt.sample_count  = static_cast<uint32_t>(
        std::min(values.size(), static_cast<std::size_t>(MAX_SAMPLES_PER_PACKET)));

    for (uint32_t i = 0; i < pkt.sample_count; ++i) {
        pkt.samples[i] = GeneratorSample{
            .timestamp_ns = static_cast<uint64_t>(i),
            .channel_id   = channel_id,
            .value        = values[i]};
    }
    batch.packets.push_back(pkt);
    return batch;
}

TEST(ProcessingEngine, ProducesAllFiveMetrics) {
    ProcessingEngine engine;
    auto batch = make_batch(1, 0, {1.0, 2.0, 3.0, 4.0});

    auto result = engine.process_batch(batch);
    ASSERT_TRUE(result.has_value()) << result.error().message;

    // Should have 5 results (one per metric for the single channel).
    EXPECT_EQ(result->size(), 5u);

    int ma_count = 0, rms_count = 0, mm_count = 0, noise_count = 0, sq_count = 0;
    for (const auto& r : *result) {
        std::visit([&](const auto& v) {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, MovingAverageResult>) { ++ma_count; }
            else if constexpr (std::is_same_v<T, RmsResult>)      { ++rms_count; }
            else if constexpr (std::is_same_v<T, MinMaxResult>)   { ++mm_count; }
            else if constexpr (std::is_same_v<T, NoiseResult>)    { ++noise_count; }
            else if constexpr (std::is_same_v<T, SignalQualityResult>) { ++sq_count; }
        }, r);
    }
    EXPECT_EQ(ma_count,    1);
    EXPECT_EQ(rms_count,   1);
    EXPECT_EQ(mm_count,    1);
    EXPECT_EQ(noise_count, 1);
    EXPECT_EQ(sq_count,    1);
}

TEST(ProcessingEngine, EmptyBatchReturnsEmptyResults) {
    ProcessingEngine engine;
    CommittedBatch empty_batch;
    empty_batch.commit_sequence = 0;

    auto result = engine.process_batch(empty_batch);
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->empty());
}

TEST(ProcessingEngine, MultipleChannelsProduceFiveEach) {
    ProcessingEngine engine;
    CommittedBatch batch;
    batch.commit_sequence = 1;

    IngestedPacket pkt{};
    pkt.sequence     = 1;
    pkt.sample_count = 4;
    // Two channels: 0 and 1, alternating.
    pkt.samples[0] = {0, 0, 1.0};
    pkt.samples[1] = {1, 1, 2.0};
    pkt.samples[2] = {2, 0, 3.0};
    pkt.samples[3] = {3, 1, 4.0};
    batch.packets.push_back(pkt);

    auto result = engine.process_batch(batch);
    ASSERT_TRUE(result.has_value());
    // 2 channels × 5 metrics = 10
    EXPECT_EQ(result->size(), 10u);
}
