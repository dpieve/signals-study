#pragma once
#include <array>
#include <atomic>
#include <span>
#include <cstdint>

// Keep render_buffers.h free of raylib headers so it can be included
// in test executables that do not link against raylib.
// WAVEFORM_BUFFER_SIZE must be a power of two for bitmask indexing.
static constexpr int WAVEFORM_BUFFER_SIZE = 4096;

/// Lock-free single-producer / single-consumer circular buffer for waveform data.
///
/// Designed for the ICU monitor rendering pipeline:
///   - gRPC receive thread writes via push()
///   - Render thread reads via get_view()
///
/// Power-of-two size enables bitmask indexing (write_pos_ & (SIZE-1))
/// which is cheaper than modulo and avoids branching.
///
/// Memory ordering contract:
///   push():     writes sample THEN fetch_add(release) on write_pos_
///   get_view(): load(acquire) write_pos_ THEN reads samples
/// This single-copy acquire/release pair provides the necessary
/// happens-before edge between writer and reader.
class ChannelRingBuffer {
public:
    /// Write one sample into the circular buffer.
    /// noexcept — must not block or allocate on the gRPC hot path.
    void push(double value) noexcept;

    /// Return an ordered span over all WAVEFORM_BUFFER_SIZE samples
    /// with the oldest sample first and the newest last (left-to-right
    /// screen order for the waveform renderer).
    ///
    /// The span aliases internal storage and remains valid until the next push().
    /// Call only from the render thread.
    [[nodiscard]] std::span<const double> get_view() const noexcept;

    /// Always WAVEFORM_BUFFER_SIZE — exposed so callers can avoid magic numbers.
    [[nodiscard]] size_t size() const noexcept { return WAVEFORM_BUFFER_SIZE; }

private:
    static constexpr size_t MASK = static_cast<size_t>(WAVEFORM_BUFFER_SIZE) - 1;
    static_assert((WAVEFORM_BUFFER_SIZE & (WAVEFORM_BUFFER_SIZE - 1)) == 0,
                  "WAVEFORM_BUFFER_SIZE must be a power of two");

    std::array<double, WAVEFORM_BUFFER_SIZE> data_{};

    // Placed on its own cache line to prevent false sharing with data_.
    alignas(64) std::atomic<size_t> write_pos_{0};

    // Scratch buffer filled by get_view() to present an ordered contiguous span.
    // Mutable so get_view() can be const while still populating it.
    mutable std::array<double, WAVEFORM_BUFFER_SIZE> view_scratch_{};
};
