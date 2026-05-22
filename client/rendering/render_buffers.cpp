#include "render_buffers.h"
#include <cstring>

void ChannelRingBuffer::push(double value) noexcept {
    // Load current write position — relaxed is fine here because we
    // are the only writer and the release below provides the edge.
    const size_t pos = write_pos_.load(std::memory_order_relaxed);
    data_[pos & MASK] = value;
    // Release: make the sample write visible to the reader before advancing.
    write_pos_.fetch_add(1, std::memory_order_release);
}

std::span<const double> ChannelRingBuffer::get_view() const noexcept {
    // Acquire: synchronise with the last release in push() so we see all
    // sample writes that happened-before this load.
    const size_t end = write_pos_.load(std::memory_order_acquire);

    // The oldest sample slot is 'end' itself (it wraps around to the position
    // that will be overwritten next, which currently holds the oldest data
    // once the buffer has been filled at least once).
    // Build a contiguous, ordered copy in view_scratch_.
    const size_t start = end; // oldest slot
    for (size_t i = 0; i < static_cast<size_t>(WAVEFORM_BUFFER_SIZE); ++i) {
        view_scratch_[i] = data_[(start + i) & MASK];
    }

    return std::span<const double>(view_scratch_.data(),
                                   static_cast<size_t>(WAVEFORM_BUFFER_SIZE));
}
