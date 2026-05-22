#include "status_bar.h"
#include "../config/render_config.h"
#include <ctime>
#include <cstdio>

void draw_status_bar(Rectangle          rect,
                     const std::string& patient_id,
                     const std::string& profile_name) {
    // Background
    DrawRectangleRec(rect, STATUS_BAR_BG);

    // Bottom separator line
    DrawLineEx(
        Vector2{rect.x,            rect.y + rect.height},
        Vector2{rect.x + rect.width, rect.y + rect.height},
        1.0f,
        Color{50, 70, 120, 255});

    const int font_size   = 18;
    const int small_size  = 14;
    const int text_y      = static_cast<int>(rect.y) + 6;
    const int text_y_sub  = static_cast<int>(rect.y) + 24;

    // -----------------------------------------------------------------------
    // Left section: patient ID + admission status
    // -----------------------------------------------------------------------
    DrawText(patient_id.c_str(),
             static_cast<int>(rect.x) + 12, text_y,
             font_size, WHITE);
    DrawText("Not Admitted",
             static_cast<int>(rect.x) + 12, text_y_sub,
             small_size, Color{180, 180, 180, 255});

    // Vertical separator
    const float sep1_x = rect.x + 220.0f;
    DrawLineEx(
        Vector2{sep1_x, rect.y + 6.0f},
        Vector2{sep1_x, rect.y + rect.height - 6.0f},
        1.0f,
        Color{60, 80, 130, 255});

    // -----------------------------------------------------------------------
    // Centre section: date / time using the system wall clock
    // -----------------------------------------------------------------------
    {
        const time_t now = static_cast<time_t>(GetTime()) + time(nullptr);
        // GetTime() returns seconds since InitWindow — use time() for wall clock
        const time_t wall_now = time(nullptr);
        struct tm    tm_buf{};
#ifdef _WIN32
        localtime_s(&tm_buf, &wall_now);
#else
        localtime_r(&wall_now, &tm_buf);
#endif
        char date_buf[32];
        char time_buf[16];
        std::strftime(date_buf, sizeof(date_buf), "%d %b %Y", &tm_buf);
        std::strftime(time_buf, sizeof(time_buf), "%H:%M:%S", &tm_buf);

        const int date_x = static_cast<int>(sep1_x) + 12;
        DrawText(date_buf, date_x, text_y,     font_size, WHITE);
        DrawText(time_buf, date_x, text_y_sub, small_size, Color{180, 180, 180, 255});
        (void)now; // suppress unused variable warning
    }

    // Vertical separator
    const float sep2_x = rect.x + 440.0f;
    DrawLineEx(
        Vector2{sep2_x, rect.y + 6.0f},
        Vector2{sep2_x, rect.y + rect.height - 6.0f},
        1.0f,
        Color{60, 80, 130, 255});

    // -----------------------------------------------------------------------
    // Right section: profile name + mode
    // -----------------------------------------------------------------------
    {
        const int profile_x = static_cast<int>(sep2_x) + 12;
        std::string profile_label = "Profile: " + profile_name;
        DrawText(profile_label.c_str(), profile_x, text_y,
                 font_size, WHITE);
        DrawText("Dynamic Waves", profile_x, text_y_sub,
                 small_size, Color{100, 200, 255, 255});
    }

    // -----------------------------------------------------------------------
    // Far-right: ICU monitor mode indicator
    // -----------------------------------------------------------------------
    {
        const char* mode_str = "LIVE";
        const int   mode_w   = MeasureText(mode_str, font_size);
        const int   mode_x   = static_cast<int>(rect.x + rect.width) - mode_w - 16;
        // Blinking green dot for LIVE
        const Color dot_color = Color{0, 255, 80, 255};
        DrawCircle(mode_x - 14, text_y + font_size / 2, 5.0f, dot_color);
        DrawText(mode_str, mode_x, text_y, font_size, dot_color);
    }
}
