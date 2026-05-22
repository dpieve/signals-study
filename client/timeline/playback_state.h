#pragma once
#include <cstdint>

/// Operating mode for the timeline controller.
enum class PlaybackMode {
    Live,        ///< Tracking the real-time live stream head
    Historical   ///< Replaying a specific historical window
};

/// Snapshot of the timeline cursor state shared between the timeline
/// controller and the gRPC client / renderer.
struct PlaybackState {
    PlaybackMode mode           = PlaybackMode::Live;
    uint64_t     current_ts_ns  = 0;   ///< Cursor timestamp (ns since epoch)
    double       playback_speed = 1.0; ///< 1.0 = real-time; 2.0 = 2× speed
};
