#include <gtest/gtest.h>
#include "../rendering/render_buffers.h"
#include <numeric>
#include <vector>

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static constexpr int BUF = WAVEFORM_BUFFER_SIZE; // 4096

// ---------------------------------------------------------------------------
// Basic push / get_view round-trip
// ---------------------------------------------------------------------------

TEST(ChannelRingBuffer, PushAndGetView_ReturnsCorrectSize) {
    ChannelRingBuffer buf;
    buf.push(1.0);
    buf.push(2.0);
    buf.push(3.0);
    const auto view = buf.get_view();
    EXPECT_EQ(static_cast<int>(view.size()), BUF);
}

// ---------------------------------------------------------------------------
// Wraparound: push BUF + 10 items; oldest should be overwritten
// ---------------------------------------------------------------------------

TEST(ChannelRingBuffer, WrapAroundOverwritesOldestSample) {
    ChannelRingBuffer buf;

    // Fill with a known sentinel
    for (int i = 0; i < BUF; ++i) {
        buf.push(static_cast<double>(i));
    }

    // Now push 10 more, overwriting the 10 oldest slots
    for (int i = 0; i < 10; ++i) {
        buf.push(9000.0 + static_cast<double>(i));
    }

    const auto view = buf.get_view();
    ASSERT_EQ(static_cast<int>(view.size()), BUF);

    // get_view() returns oldest-first, newest-last.
    // After BUF+10 pushes, the oldest sample is 10 (index 0 in view),
    // and the newest is 9009 (index BUF-1 in view).
    // Slots 0..BUF-11 in view hold the old values 10..BUF-1.
    for (int i = 0; i < BUF - 10; ++i) {
        EXPECT_DOUBLE_EQ(view[static_cast<size_t>(i)],
                         static_cast<double>(10 + i))
            << "Mismatch at view[" << i << "]";
    }

    // Last 10 slots hold the newest pushes: 9000..9009.
    for (int i = 0; i < 10; ++i) {
        EXPECT_DOUBLE_EQ(view[static_cast<size_t>(BUF - 10 + i)],
                         9000.0 + static_cast<double>(i))
            << "Mismatch at view[" << (BUF - 10 + i) << "]";
    }
}

// ---------------------------------------------------------------------------
// Ordering: newest sample always at the tail of get_view()
// ---------------------------------------------------------------------------

TEST(ChannelRingBuffer, NewestSampleIsAtEndOfView) {
    ChannelRingBuffer buf;

    for (int i = 0; i < BUF * 2; ++i) {
        buf.push(static_cast<double>(i));
    }

    const auto view = buf.get_view();
    // Last pushed value is BUF*2 - 1
    EXPECT_DOUBLE_EQ(view[static_cast<size_t>(BUF - 1)],
                     static_cast<double>(BUF * 2 - 1));
}

// ---------------------------------------------------------------------------
// Power-of-two invariant
// ---------------------------------------------------------------------------

TEST(ChannelRingBuffer, BufferSizeIsPowerOfTwo) {
    EXPECT_GT(BUF, 0);
    EXPECT_EQ(BUF & (BUF - 1), 0) << "WAVEFORM_BUFFER_SIZE must be a power of two";
}

// ---------------------------------------------------------------------------
// Partial fill: before BUF pushes the unwritten slots should be zero
// ---------------------------------------------------------------------------

TEST(ChannelRingBuffer, UnwrittenSlotsAreZero) {
    ChannelRingBuffer buf; // default-initialised to 0.0
    buf.push(42.0);

    const auto view = buf.get_view();
    // All but the last slot should still be 0.0
    // (write_pos_ == 1, so oldest = slot 1, newest = slot 0 = 42.0)
    EXPECT_DOUBLE_EQ(view[static_cast<size_t>(BUF - 1)], 42.0);
    for (int i = 0; i < BUF - 1; ++i) {
        EXPECT_DOUBLE_EQ(view[static_cast<size_t>(i)], 0.0);
    }
}

// ---------------------------------------------------------------------------
// Repeated push past multiple full cycles
// ---------------------------------------------------------------------------

TEST(ChannelRingBuffer, MultipleCyclesRemainConsistent) {
    ChannelRingBuffer buf;

    constexpr int CYCLES = 5;
    for (int c = 0; c < CYCLES; ++c) {
        for (int i = 0; i < BUF; ++i) {
            buf.push(static_cast<double>(c * BUF + i));
        }
    }

    const auto view = buf.get_view();
    // After 5 full cycles the buffer should contain the last BUF values:
    // (CYCLES-1)*BUF .. CYCLES*BUF - 1
    const int base = (CYCLES - 1) * BUF;
    for (int i = 0; i < BUF; ++i) {
        EXPECT_DOUBLE_EQ(view[static_cast<size_t>(i)],
                         static_cast<double>(base + i))
            << "Mismatch at index " << i;
    }
}
