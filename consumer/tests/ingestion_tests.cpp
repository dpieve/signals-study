#include <gtest/gtest.h>
#include <thread>
#include <vector>
#include <atomic>
#include <unordered_set>
#include "../ingestion/packet_queue.h"
#include "../common/telemetry_types.h"

// ---------------------------------------------------------------------------
// Multi-threaded stress: 4 producers × 10,000 packets, 1 consumer
// ---------------------------------------------------------------------------

TEST(IngestionStress, MultiProducerSingleConsumer) {
    constexpr int kProducers    = 4;
    constexpr int kPktsPerProd  = 10'000;
    constexpr int kTotalPkts    = kProducers * kPktsPerProd;
    constexpr std::size_t kCap  = 65536;

    PacketQueue q(kCap);

    std::atomic<int> produced{0};

    // Producers
    std::vector<std::thread> producers;
    producers.reserve(kProducers);
    for (int t = 0; t < kProducers; ++t) {
        producers.emplace_back([&q, &produced, t] {
            for (int i = 0; i < kPktsPerProd; ++i) {
                IngestedPacket pkt{};
                // Encode producer ID in high 32 bits, sequence in low 32 bits
                // so every packet has a unique sequence for de-dup checking.
                pkt.sequence = (static_cast<uint64_t>(t) << 32)
                               | static_cast<uint64_t>(i);
                // Retry until enqueue succeeds (capacity large enough to absorb
                // all packets without drops).
                while (!q.enqueue(std::move(pkt))) {
                    std::this_thread::yield();
                }
                produced.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }

    // Consumer
    std::unordered_set<uint64_t> seen;
    seen.reserve(kTotalPkts);
    int consumed = 0;

    std::stop_source ss;
    IngestedPacket out{};

    // Run consumer until we have all packets (producers join before we stop).
    std::thread consumer_thread([&] {
        while (consumed < kTotalPkts) {
            if (q.wait_dequeue_timed(out, ss.get_token())) {
                EXPECT_TRUE(seen.insert(out.sequence).second)
                    << "Duplicate sequence: " << out.sequence;
                ++consumed;
            }
        }
    });

    for (auto& p : producers) { p.join(); }
    consumer_thread.join();

    EXPECT_EQ(consumed, kTotalPkts);
    EXPECT_EQ(static_cast<int>(seen.size()), kTotalPkts);
    EXPECT_EQ(q.drop_count.load(), 0u);
}

// ---------------------------------------------------------------------------
// Queue capacity: fill to cap, verify drop_count > 0, producer never blocks
// ---------------------------------------------------------------------------

TEST(IngestionStress, QueueCapacityNeverBlocks) {
    constexpr std::size_t kCap   = 64;
    constexpr std::size_t kExtra = 32;
    PacketQueue q(kCap);

    std::size_t enqueued = 0;
    for (std::size_t i = 0; i < kCap + kExtra; ++i) {
        IngestedPacket pkt{};
        pkt.sequence = i;
        // Must return immediately (no blocking).
        if (q.enqueue(std::move(pkt))) { ++enqueued; }
    }

    EXPECT_EQ(enqueued, kCap);
    EXPECT_GT(q.drop_count.load(), 0u);
    EXPECT_EQ(q.drop_count.load(), kExtra);
}
