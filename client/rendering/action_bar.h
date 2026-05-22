#pragma once
#include <raylib.h>
#include <string>
#include <vector>

/// One hardware-style button in the bottom action bar.
struct ActionButton {
    std::string label;  ///< Display text, e.g. "Main View"
    bool        active; ///< Highlighted / currently selected state
};

/// Render the bottom action bar with hardware-style rectangular buttons.
///
/// Buttons are evenly spaced across the full bar width.
/// @p hovered_idx is written with the index of the button under the mouse
/// cursor, or -1 if none. Pass nullptr to skip hover detection.
void draw_action_bar(Rectangle                       rect,
                     const std::vector<ActionButton>& buttons,
                     int*                             hovered_idx);
