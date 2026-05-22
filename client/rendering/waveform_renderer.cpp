#include "waveform_renderer.h"
#include "../config/render_config.h"
#include <algorithm>
#include <cmath>

WaveformRenderer::WaveformRenderer(int width, int height)
    : width_(width), height_(height) {}

void WaveformRenderer::draw_waveform(std::span<const double> data,
                                     Rectangle               dst,
                                     Color                   color,
                                     float                   scale,
                                     float                   offset) {
    if (data.empty()) return;

    const float rect_w   = dst.width;
    const float rect_h   = dst.height;
    const float centre_y = dst.y + rect_h * (0.5f - offset * 0.1f);

    // Number of screen columns available — limited by both the rectangle width
    // and the preallocated points_ capacity.
    const int num_pixels = static_cast<int>(
        std::min(static_cast<float>(points_.size()), rect_w));

    if (num_pixels <= 0) return;

    const size_t data_size  = data.size();
    const float  x_step     = rect_w / static_cast<float>(num_pixels - 1);

    // Half-height used for normalisation — scale contracts / expands this.
    const float half_h = (rect_h * 0.5f) * scale;

    int point_count = 0;

    for (int px = 0; px < num_pixels; ++px) {
        // Map screen column px to a data index — oldest data at left (px 0),
        // newest data at right (px num_pixels-1).
        const size_t data_idx = static_cast<size_t>(
            static_cast<float>(data_size - 1) *
            static_cast<float>(px) / static_cast<float>(num_pixels - 1));

        const double sample = data[data_idx];

        // Clamp the normalised value to [-1, 1] before mapping to pixels.
        const float norm_val = std::clamp(static_cast<float>(sample), -1.0f, 1.0f);

        const float screen_x = dst.x + static_cast<float>(px) * x_step;
        // Positive sample → up on screen (subtract from centre)
        const float screen_y = centre_y - norm_val * half_h;

        points_[static_cast<size_t>(point_count)] = Vector2{screen_x, screen_y};
        ++point_count;
    }

    if (point_count < 2) return;

    // Use DrawLineStrip for the bulk of the waveform (efficient single call).
    // Raylib's DrawLineStrip uses GL_LINE_STRIP which is hardware-antialiased
    // on most platforms when MSAA is enabled.
    DrawLineStrip(points_.data(), point_count, color);

    // Additionally draw each segment with DrawLineEx (thickness 1.5) for
    // software anti-aliasing on platforms that don't support MSAA.
    // This is slightly redundant but ensures smooth lines everywhere.
    for (int i = 0; i < point_count - 1; ++i) {
        DrawLineEx(points_[static_cast<size_t>(i)],
                   points_[static_cast<size_t>(i + 1)],
                   1.5f,
                   color);
    }
}

void WaveformRenderer::draw_grid(Rectangle dst) {
    // Horizontal lines at 25 % intervals (baseline + 25, 50, 75 % of height)
    for (int row = 1; row < 4; ++row) {
        const float y = dst.y + dst.height * static_cast<float>(row) * 0.25f;
        DrawLineEx(
            Vector2{dst.x,            y},
            Vector2{dst.x + dst.width, y},
            1.0f,
            GRID_COLOR);
    }

    // Vertical lines every ~100 pixels
    const int v_step = 100;
    for (float x = dst.x; x < dst.x + dst.width; x += static_cast<float>(v_step)) {
        DrawLineEx(
            Vector2{x, dst.y},
            Vector2{x, dst.y + dst.height},
            1.0f,
            GRID_COLOR);
    }
}
