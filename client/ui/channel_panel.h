#pragma once
#include <raylib.h>
#include <string>
#include "../common/telemetry_types.h"

/// Describes the visual slice assigned to one acquisition channel.
///
/// The TelemetryView subdivides the waveform_area into N equal horizontal
/// bands — one ChannelPanel per channel.  The WaveformRenderer draws into
/// ChannelPanel::bounds.
struct ChannelPanel {
    ChannelId   id;     ///< Logical channel identifier
    std::string label;  ///< Human-readable label shown at the left edge
    Color       color;  ///< ICU-convention waveform colour
    Rectangle   bounds; ///< Screen rectangle for this channel's waveform strip
};
