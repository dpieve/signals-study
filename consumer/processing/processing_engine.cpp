#include "processing_engine.h"
#include "signal_metrics.h"
#include <spdlog/spdlog.h>

std::expected<std::vector<ProcessingResult>, ProcessingError>
ProcessingEngine::process_batch(const CommittedBatch& batch) {
    if (batch.packets.empty()) {
        return std::vector<ProcessingResult>{};
    }

    // Accumulate samples into per-channel history.
    for (const auto& pkt : batch.packets) {
        const uint32_t count =
            std::min(pkt.sample_count, MAX_SAMPLES_PER_PACKET);
        for (uint32_t i = 0; i < count; ++i) {
            const auto& s  = pkt.samples[i];
            const auto  ch = static_cast<ChannelId>(s.channel_id);
            channel_samples_[ch].push_back(s.value);
        }
    }

    std::vector<ProcessingResult> results;
    results.reserve(channel_samples_.size() * 5);

    for (const auto& [channel, samples] : channel_samples_) {
        if (samples.empty()) { continue; }

        std::span<const double> sp{samples};

        results.emplace_back(MovingAverageResult{
            channel, moving_average(sp, kMovingAverageWindow)});

        results.emplace_back(RmsResult{
            channel, rms(sp)});

        {
            auto [lo, hi] = min_max(sp);
            results.emplace_back(MinMaxResult{channel, lo, hi});
        }

        results.emplace_back(NoiseResult{
            channel, noise_level(sp)});

        results.emplace_back(SignalQualityResult{
            channel, signal_quality(sp)});
    }

    spdlog::debug("ProcessingEngine: batch {} — {} channels, {} results",
                  batch.commit_sequence,
                  channel_samples_.size(),
                  results.size());

    return results;
}
