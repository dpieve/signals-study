#pragma once
#include <vector>
#include <map>
#include <mutex>
#include <cmath>
#include "../config/client_config.h"
#include "../config/render_config.h"
#include "../rendering/render_buffers.h"
#include "../rendering/waveform_renderer.h"
#include "../rendering/monitor_layout.h"
#include "../rendering/numeric_panel.h"
#include "../rendering/status_bar.h"
#include "../rendering/action_bar.h"
#include "../timeline/timeline_controller.h"
#include "channel_panel.h"
#include "telemetry.pb.h"

/// Top-level composition of the ICU monitor UI.
///
/// Thread safety model:
///   on_packet()  — called from the gRPC receive thread.
///   render()     — called from the Raylib main/render thread.
///
/// Ring buffers use atomic write_pos_ (acquire/release) so no mutex is needed
/// for waveform data.  The latest_values_ map (used for numeric panel) is
/// protected by latest_mutex_.
class TelemetryView {
public:
    explicit TelemetryView(ClientConfig cfg);

    /// Ingest a gRPC Packet — called from the gRPC background thread.
    void on_packet(const telemetry::Packet& pkt);

    /// Draw the full screen for this frame — called from the render thread.
    void render(int window_width, int window_height);

private:
    ClientConfig config_;

    // One ring buffer per channel — written by gRPC thread, read by render thread
    std::vector<ChannelRingBuffer> ring_buffers_;

    WaveformRenderer    waveform_renderer_;
    TimelineController  timeline_;

    // Channel panel layout (computed in render() when window size changes)
    std::vector<ChannelPanel> channel_panels_;
    int last_waveform_w_ = 0;
    int last_waveform_h_ = 0;

    // Latest per-channel value for the numeric panel (protected by mutex)
    std::map<uint32_t, double> latest_values_;
    mutable std::mutex         latest_mutex_;

    // Assigns ICU-convention colours to channels by index
    static Color channel_color(uint32_t index) noexcept;

    // Rebuilds channel_panels_ when the waveform area changes size
    void rebuild_panels(Rectangle waveform_area);
};
