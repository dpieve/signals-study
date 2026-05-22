#include "timeline_controller.h"
#include <algorithm>

void TimelineController::set_live() {
    state_.mode          = PlaybackMode::Live;
    state_.current_ts_ns = latest_ts_ns_;
    state_.playback_speed = 1.0;
}

void TimelineController::set_historical(uint64_t start_ts_ns) {
    state_.mode           = PlaybackMode::Historical;
    state_.current_ts_ns  = start_ts_ns;
    state_.playback_speed = 1.0;
}

void TimelineController::scroll_by(int64_t delta_ns) {
    if (state_.mode != PlaybackMode::Historical) return;

    // Prevent scrolling before the epoch (t=0) or past the live head.
    const int64_t current = static_cast<int64_t>(state_.current_ts_ns);
    const int64_t result  = current + delta_ns;

    if (result < 0) {
        state_.current_ts_ns = 0;
    } else if (static_cast<uint64_t>(result) > latest_ts_ns_) {
        state_.current_ts_ns = latest_ts_ns_;
    } else {
        state_.current_ts_ns = static_cast<uint64_t>(result);
    }
}

void TimelineController::on_new_sample(uint64_t ts_ns) noexcept {
    if (ts_ns > latest_ts_ns_) {
        latest_ts_ns_ = ts_ns;
    }
    // In live mode, keep the cursor at the data head.
    if (state_.mode == PlaybackMode::Live) {
        state_.current_ts_ns = latest_ts_ns_;
    }
}
