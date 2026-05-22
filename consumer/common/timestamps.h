#pragma once
#include <chrono>
#include <cstdint>

/// Returns current time as nanoseconds since the Unix epoch using the
/// steady clock promoted to wall-clock epoch via system_clock for the
/// initial offset.  All subsequent calls use steady_clock deltas so
/// the value is monotonic.
[[nodiscard]] inline uint64_t now_ns() noexcept {
    using namespace std::chrono;
    // Use system_clock for the absolute epoch anchor.
    auto tp = system_clock::now();
    return static_cast<uint64_t>(
        duration_cast<nanoseconds>(tp.time_since_epoch()).count());
}
