#include <gtest/gtest.h>
#include "../ingestion/packet_queue.h"
#include "../common/telemetry_types.h"

// ---------------------------------------------------------------------------
// Helper
// ---------------------------------------------------------------------------

static IngestedPacket make_packet(uint64_t seq) {
    IngestedPacket pkt{};
    pkt.sequence = seq;
    return pkt;
}

// ---------------------------------------------------------------------------
// Single-thread enqueue / dequeue correctness
// ---------------------------------------------------------------------------

TEST(PacketQueue, EnqueueDequeueRoundTrip) {
    PacketQueue q(8);

    for (uint64_t i = 0; i < 4; ++i) {
        EXPECT_TRUE(q.enqueue(make_packet(i)));
    }

    std::stop_source ss;
    IngestedPacket out{};
    for (uint64_t i = 0; i < 4; ++i) {
        EXPECT_TRUE(q.wait_dequeue_timed(out, ss.get_token()));
        EXPECT_EQ(out.sequence, i);
    }

    EXPECT_EQ(q.drop_count.load(), 0u);
}

// ---------------------------------------------------------------------------
// Fill to capacity — enqueue must return false, never block
// ---------------------------------------------------------------------------

TEST(PacketQueue, CapacityEnforced) {
    constexpr std::size_t kCap = 16;
    PacketQueue q(kCap);

    std::size_t accepted = 0;
    for (std::size_t i = 0; i < kCap * 2; ++i) {
        if (q.enqueue(make_packet(static_cast<uint64_t>(i)))) {
            ++accepted;
        }
    }

    EXPECT_EQ(accepted, kCap);
    EXPECT_EQ(q.drop_count.load(), kCap);
}

// ---------------------------------------------------------------------------
// drop_count increments on overflow
// ---------------------------------------------------------------------------

TEST(PacketQueue, DropCountIncrements) {
    PacketQueue q(4);

    for (int i = 0; i < 10; ++i) {
        (void)q.enqueue(make_packet(static_cast<uint64_t>(i)));
    }

    EXPECT_EQ(q.drop_count.load(), 6u);
}

// ---------------------------------------------------------------------------
// Stop token causes wait_dequeue_timed to return false
// ---------------------------------------------------------------------------

TEST(PacketQueue, StopTokenHonoured) {
    PacketQueue q(8);  // empty queue

    std::stop_source ss;
    ss.request_stop();  // already stopped

    IngestedPacket out{};
    EXPECT_FALSE(q.wait_dequeue_timed(out, ss.get_token()));
}
