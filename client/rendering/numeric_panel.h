#pragma once
#include <raylib.h>
#include <string>
#include <vector>

/// One entry in the right-side numeric vitals panel.
struct NumericEntry {
    std::string label; ///< Short channel name, e.g. "HR", "SpO2"
    double      value; ///< Current value to display
    Color       color; ///< ICU-convention channel colour
    std::string unit;  ///< Unit string, e.g. "bpm", "%", "mmHg"
};

/// Render the right-side numeric panel.
///
/// Stacks each NumericEntry vertically with a divider line between entries.
/// Renders: small label, large value, small unit — all on a dark background.
void draw_numeric_panel(Rectangle                        panel_rect,
                        const std::vector<NumericEntry>& entries);
