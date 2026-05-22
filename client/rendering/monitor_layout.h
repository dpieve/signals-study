#pragma once
#include <raylib.h>
#include "../config/render_config.h"

/// Screen layout computed fresh each frame from the current window dimensions.
/// All regions are non-overlapping and together cover the full window.
struct MonitorLayout {
    Rectangle status_bar;    ///< Top strip: patient/time info
    Rectangle waveform_area; ///< Dominant centre-left waveform canvas
    Rectangle numeric_panel; ///< Right-side large-number vitals panel
    Rectangle action_bar;    ///< Bottom hardware-style button row
};

/// Derive the four layout rectangles from the current window size.
/// Uses the FRAC constants from render_config.h for proportional sizing.
[[nodiscard]] MonitorLayout compute_layout(int window_width, int window_height);
