#pragma once
#include <atomic>
#include <cstdint>
#include "packet_queue.h"
#include "../../generator/generator.h"

/// Bridges the C generator callback API to the lock-free PacketQueue.
///
/// packet_callback is a static C-linkage function that can be passed
/// directly to generator_start().  The user_data pointer must point to
/// a live IngestionManager instance.
class IngestionManager {
public:
    explicit IngestionManager(PacketQueue& queue);

    /// C callback registered with generator_start().
    static void packet_callback(const GeneratorPacket* pkt,
                                void* user_data) noexcept;

    /// Number of packets dropped at the ingestion layer (queue full).
    [[nodiscard]] uint64_t ingestion_drop_count() const noexcept {
        return ingestion_drops_.load(std::memory_order_relaxed);
    }

private:
    PacketQueue& queue_;
    std::atomic<uint64_t> ingestion_drops_{0};
    std::atomic<uint64_t> total_received_{0};
};
