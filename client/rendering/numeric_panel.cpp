#include "numeric_panel.h"
#include "../config/render_config.h"
#include <cstdio>
#include <cmath>
#include <algorithm>

void draw_numeric_panel(Rectangle                        panel_rect,
                        const std::vector<NumericEntry>& entries) {
    // Dark background for the entire panel
    DrawRectangleRec(panel_rect, NUMERIC_PANEL_BG);

    // Thin left border to visually separate from waveform area
    DrawLineEx(
        Vector2{panel_rect.x, panel_rect.y},
        Vector2{panel_rect.x, panel_rect.y + panel_rect.height},
        2.0f,
        Color{40, 60, 90, 255});

    if (entries.empty()) return;

    const int   n           = static_cast<int>(entries.size());
    const float cell_height = panel_rect.height / static_cast<float>(n);

    for (int i = 0; i < n; ++i) {
        const NumericEntry& entry = entries[static_cast<size_t>(i)];
        const float cell_y = panel_rect.y + static_cast<float>(i) * cell_height;

        // Cell background — alternating very subtle shade for readability
        const Color cell_bg = (i % 2 == 0)
            ? NUMERIC_PANEL_BG
            : Color{14, 18, 30, 255};
        DrawRectangle(
            static_cast<int>(panel_rect.x),
            static_cast<int>(cell_y),
            static_cast<int>(panel_rect.width),
            static_cast<int>(cell_height),
            cell_bg);

        const int pad_x = static_cast<int>(panel_rect.x) + 8;
        int       cur_y = static_cast<int>(cell_y) + 8;

        // --- Label (small, white) ---
        const int label_font_size = 16;
        DrawText(entry.label.c_str(), pad_x, cur_y, label_font_size, WHITE);
        cur_y += label_font_size + 4;

        // --- Value (large, channel color) ---
        // Format to 1 decimal place; show "--" if value is NaN
        char value_buf[32];
        if (std::isnan(entry.value)) {
            std::snprintf(value_buf, sizeof(value_buf), "--");
        } else {
            std::snprintf(value_buf, sizeof(value_buf), "%.1f", entry.value);
        }
        const int value_font_size = 40;
        DrawText(value_buf, pad_x, cur_y, value_font_size, entry.color);
        cur_y += value_font_size + 2;

        // --- Unit (small, dimmed white) ---
        const int unit_font_size = 14;
        DrawText(entry.unit.c_str(), pad_x, cur_y,
                 unit_font_size, Color{180, 180, 180, 255});

        // Divider line between cells (skip after last)
        if (i < n - 1) {
            const float div_y = cell_y + cell_height;
            DrawLineEx(
                Vector2{panel_rect.x,                   div_y},
                Vector2{panel_rect.x + panel_rect.width, div_y},
                1.0f,
                Color{40, 60, 90, 200});
        }
    }
}
