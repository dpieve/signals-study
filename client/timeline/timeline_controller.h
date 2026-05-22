#pragma once
#include "playback_state.h"
#include <cstdint>

/// Manages the timeline cursor for the ICU monitor.
///
/// Thread safety: all methods are intended to be called from the render thread
/// only. The gRPC thread may only call on_new_sample().
class TimelineController {
public:
    TimelineController() = default;

    /// Switch to live mode — cursor tracks the incoming data head.
    void set_live();

    /// Switch to historical mode starting at @p start_ts_ns.
    void set_historical(uint64_t start_ts_ns);

    /// Shift the cursor by @p delta_ns nanoseconds (positive = forward).
    /// In live mode this call is ignored.
    void scroll_by(int64_t delta_ns);

    /// Called from the gRPC receive thread when a new sample arrives.
    /// Updates the internal latest timestamp — used to advance the cursor
    /// when in live mode.
    void on_new_sample(uint64_t ts_ns) noexcept;

    /// Read-only access to the current timeline state.
    [[nodiscard]] const PlaybackState& state() const noexcept { return state_; }

private:
    PlaybackState state_;
    uint64_t      latest_ts_ns_ = 0;
};
