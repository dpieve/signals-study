#include "ingestion_manager.h"
#include <cstring>
#include <algorithm>
#include <spdlog/spdlog.h>

IngestionManager::IngestionManager(PacketQueue& queue)
    : queue_(queue) {}

void IngestionManager::packet_callback(const GeneratorPacket* pkt,
                                       void* user_data) noexcept {
    if (!pkt || !user_data) [[unlikely]] { return; }

    auto* self = static_cast<IngestionManager*>(user_data);

    IngestedPacket ingested{};
    ingested.sequence   = pkt->sequence;
    ingested.has_gap    = pkt->has_gap;
    ingested.sample_count = std::min(pkt->sample_count,
                                     MAX_SAMPLES_PER_PACKET);

    if (pkt->samples && ingested.sample_count > 0) {
        ingested.first_timestamp_ns = pkt->samples[0].timestamp_ns;
        std::memcpy(ingested.samples.data(),
                    pkt->samples,
                    ingested.sample_count * sizeof(GeneratorSample));
    }

    const uint64_t received =
        self->total_received_.fetch_add(1, std::memory_order_relaxed) + 1;

    if (!self->queue_.enqueue(std::move(ingested))) {
        self->ingestion_drops_.fetch_add(1, std::memory_order_relaxed);
    }

    // Periodic queue-depth logging (every 1000 packets).
    if (received % 1000 == 0) [[unlikely]] {
        spdlog::debug("IngestionManager: {} packets received, {} dropped "
                      "(queue drops: {})",
                      received,
                      self->ingestion_drops_.load(std::memory_order_relaxed),
                      self->queue_.drop_count.load(std::memory_order_relaxed));
    }
}
