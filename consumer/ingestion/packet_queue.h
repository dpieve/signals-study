#pragma once
#include <atomic>
#include <stop_token>
#include <cstddef>
#include <concurrentqueue.h>
#include "../common/telemetry_types.h"

/// A bounded, lock-free MPSC/MPMC ingestion queue backed by
/// moodycamel::ConcurrentQueue.
///
/// Capacity is enforced via an atomic counter; producers that would exceed
/// the limit receive a false return value and the drop_count is incremented.
class PacketQueue {
public:
    /// @param capacity  Maximum number of IngestedPacket items in flight.
    explicit PacketQueue(std::size_t capacity);

    /// Attempt to enqueue a packet.
    /// @return true on success; false if the queue is at capacity (packet dropped).
    [[nodiscard]] bool enqueue(IngestedPacket&& pkt) noexcept;

    /// Block until a packet is available or the stop token is signalled.
    /// Uses an internal 1 ms spin-wait so it never blocks indefinitely.
    /// @return true if a packet was dequeued; false if stop was requested.
    bool wait_dequeue_timed(IngestedPacket& out, std::stop_token st);

    /// Total number of packets dropped due to capacity overflow (ever).
    std::atomic<uint64_t> drop_count{0};

private:
    std::size_t capacity_;
    std::atomic<std::size_t> size_{0};
    moodycamel::ConcurrentQueue<IngestedPacket> queue_;
};
