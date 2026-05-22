#pragma once
#include <raylib.h>
#include <array>
#include <span>
#include <cstdint>

/// Stateless waveform rendering utility.
///
/// Holds a single preallocated geometry scratch buffer (points_) that is
/// reused each frame to avoid per-frame heap allocation.
/// A single WaveformRenderer instance is shared across all channels.
class WaveformRenderer {
public:
    /// @param width   Initial window width (unused at runtime; reserved for future RT texture)
    /// @param height  Initial window height
    explicit WaveformRenderer(int width, int height);
    ~WaveformRenderer() = default;

    // Non-copyable — owns preallocated geometry memory
    WaveformRenderer(const WaveformRenderer&) = delete;
    WaveformRenderer& operator=(const WaveformRenderer&) = delete;

    /// Draw a waveform from the ring-buffer ordered data into @p dst.
    ///
    /// @param data    Ordered span — index 0 is the oldest sample (left edge),
    ///                last index is the newest sample (right edge).
    /// @param dst     Screen rectangle to render into; samples are clipped to this rect.
    /// @param color   ICU-convention channel color from render_config.h
    /// @param scale   Vertical scale multiplier (1.0 = fills ±half the rect height)
    /// @param offset  Vertical baseline offset in [-1, 1]; 0 = centred
    void draw_waveform(std::span<const double> data,
                       Rectangle               dst,
                       Color                   color,
                       float                   scale  = 1.0f,
                       float                   offset = 0.0f);

    /// Draw a subtle ECG-paper-style grid inside @p dst.
    /// Uses GRID_COLOR from render_config.h.
    void draw_grid(Rectangle dst);

private:
    // Preallocated geometry buffer — one entry per pixel column (up to 4096)
    std::array<Vector2, 4096> points_{};

    int width_;
    int height_;
};
