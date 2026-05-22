#include "action_bar.h"
#include "../config/render_config.h"

void draw_action_bar(Rectangle                        rect,
                     const std::vector<ActionButton>& buttons,
                     int*                             hovered_idx) {
    // Background
    DrawRectangleRec(rect, ACTION_BAR_BG);

    // Top separator line
    DrawLineEx(
        Vector2{rect.x,             rect.y},
        Vector2{rect.x + rect.width, rect.y},
        1.0f,
        Color{50, 70, 120, 255});

    if (hovered_idx) *hovered_idx = -1;
    if (buttons.empty()) return;

    const int   n          = static_cast<int>(buttons.size());
    const float btn_w      = rect.width  / static_cast<float>(n);
    const float btn_h      = rect.height - 8.0f;
    const float btn_y      = rect.y + 4.0f;
    const int   font_size  = 14;

    // Mouse position for hover detection
    const Vector2 mouse = GetMousePosition();

    for (int i = 0; i < n; ++i) {
        const ActionButton& btn = buttons[static_cast<size_t>(i)];
        const float btn_x = rect.x + static_cast<float>(i) * btn_w + 2.0f;
        const float btn_actual_w = btn_w - 4.0f;

        const Rectangle btn_rect{btn_x, btn_y, btn_actual_w, btn_h};

        // Hover detection
        const bool hovered = CheckCollisionPointRec(mouse, btn_rect);
        if (hovered && hovered_idx) *hovered_idx = i;

        // Background colour
        Color bg_color;
        if (btn.active) {
            bg_color = Color{40, 80, 160, 255};  // bright active state
        } else if (hovered) {
            bg_color = Color{35, 55, 100, 255};  // hover highlight
        } else {
            bg_color = Color{25, 38, 70,  255};  // normal dark blue-gray
        }
        DrawRectangleRec(btn_rect, bg_color);

        // Border — brighter when active
        const Color border_color = btn.active
            ? Color{80, 140, 255, 255}
            : Color{55,  80, 140, 255};
        DrawRectangleLinesEx(btn_rect, 1.0f, border_color);

        // Small icon placeholder — a simple filled rectangle in the button colour
        const float icon_size = 8.0f;
        const float icon_x    = btn_x + 8.0f;
        const float icon_y    = btn_y + (btn_h - icon_size) * 0.5f;
        DrawRectangle(
            static_cast<int>(icon_x),
            static_cast<int>(icon_y),
            static_cast<int>(icon_size),
            static_cast<int>(icon_size),
            border_color);

        // Label text — centred vertically; offset right past icon
        const int text_x = static_cast<int>(btn_x + 20.0f);
        const int text_y = static_cast<int>(btn_y + (btn_h - static_cast<float>(font_size)) * 0.5f);
        DrawText(btn.label.c_str(), text_x, text_y, font_size,
                 btn.active ? WHITE : Color{200, 210, 230, 255});
    }
}
