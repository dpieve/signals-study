#include "monitor_layout.h"

MonitorLayout compute_layout(int window_width, int window_height) {
    const float fw = static_cast<float>(window_width);
    const float fh = static_cast<float>(window_height);

    // -----------------------------------------------------------------------
    // Top status bar — full width, fixed height at top
    // -----------------------------------------------------------------------
    const Rectangle status_bar{
        0.0f,
        0.0f,
        fw,
        STATUS_BAR_HEIGHT
    };

    // -----------------------------------------------------------------------
    // Bottom action bar — full width, fixed height at bottom
    // -----------------------------------------------------------------------
    const Rectangle action_bar{
        0.0f,
        fh - ACTION_BAR_HEIGHT,
        fw,
        ACTION_BAR_HEIGHT
    };

    // -----------------------------------------------------------------------
    // Middle band — everything between status bar and action bar
    // -----------------------------------------------------------------------
    const float middle_y      = STATUS_BAR_HEIGHT;
    const float middle_height = fh - STATUS_BAR_HEIGHT - ACTION_BAR_HEIGHT;

    // Numeric panel occupies a fixed fraction of the width on the right
    const float numeric_width = fw * NUMERIC_PANEL_WIDTH_FRAC;

    // Waveform area fills the remaining left portion
    const float waveform_width = fw - numeric_width;

    const Rectangle waveform_area{
        0.0f,
        middle_y,
        waveform_width,
        middle_height
    };

    const Rectangle numeric_panel{
        waveform_width,
        middle_y,
        numeric_width,
        middle_height
    };

    return MonitorLayout{status_bar, waveform_area, numeric_panel, action_bar};
}
