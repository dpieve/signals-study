#include "telemetry_view.h"
#include "../common/logging.h"
#include <algorithm>
#include <string>

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

TelemetryView::TelemetryView(ClientConfig cfg)
    : config_(std::move(cfg))
    , ring_buffers_(config_.channel_count)
    , waveform_renderer_(config_.window_width, config_.window_height) {
    // Pre-populate latest_values_ with NaN so the numeric panel can show "--"
    // until real data arrives.
    for (uint32_t i = 0; i < config_.channel_count; ++i) {
        latest_values_[i] = std::numeric_limits<double>::quiet_NaN();
    }
}

// ---------------------------------------------------------------------------
// Thread-safe packet ingestion (gRPC thread)
// ---------------------------------------------------------------------------

void TelemetryView::on_packet(const telemetry::Packet& pkt) {
    for (const auto& sample : pkt.samples()) {
        const uint32_t ch = sample.channel_id();
        if (ch >= config_.channel_count) continue;

        // Push into ring buffer (lock-free, atomic write_pos_)
        ring_buffers_[ch].push(sample.value());

        // Update latest value for the numeric panel (mutex-protected)
        {
            std::lock_guard<std::mutex> lock(latest_mutex_);
            latest_values_[ch] = sample.value();
        }

        // Advance timeline cursor
        timeline_.on_new_sample(sample.timestamp_ns());
    }
}

// ---------------------------------------------------------------------------
// Per-frame render (render thread)
// ---------------------------------------------------------------------------

void TelemetryView::render(int window_width, int window_height) {
    const MonitorLayout layout = compute_layout(window_width, window_height);

    // Rebuild channel panels if the waveform area changed
    const int wa_w = static_cast<int>(layout.waveform_area.width);
    const int wa_h = static_cast<int>(layout.waveform_area.height);
    if (wa_w != last_waveform_w_ || wa_h != last_waveform_h_) {
        rebuild_panels(layout.waveform_area);
        last_waveform_w_ = wa_w;
        last_waveform_h_ = wa_h;
    }

    // -----------------------------------------------------------------------
    // Waveform area background
    // -----------------------------------------------------------------------
    DrawRectangleRec(layout.waveform_area, MONITOR_BG);

    // -----------------------------------------------------------------------
    // Per-channel grid + waveform
    // -----------------------------------------------------------------------
    for (const ChannelPanel& panel : channel_panels_) {
        const uint32_t idx = static_cast<uint32_t>(panel.id);
        if (idx >= config_.channel_count) continue;

        // Grid (drawn first, underneath the waveform line)
        waveform_renderer_.draw_grid(panel.bounds);

        // Channel label at the left edge
        DrawText(panel.label.c_str(),
                 static_cast<int>(panel.bounds.x) + 4,
                 static_cast<int>(panel.bounds.y) + 4,
                 13, panel.color);

        // Waveform line
        const auto data = ring_buffers_[idx].get_view();
        waveform_renderer_.draw_waveform(data, panel.bounds, panel.color,
                                         0.85f, 0.0f);

        // Horizontal separator between channels (not after the last one)
        if (idx + 1 < config_.channel_count) {
            const float sep_y = panel.bounds.y + panel.bounds.height;
            DrawLineEx(
                Vector2{panel.bounds.x,                       sep_y},
                Vector2{panel.bounds.x + panel.bounds.width,  sep_y},
                1.0f,
                Color{40, 50, 40, 180});
        }
    }

    // -----------------------------------------------------------------------
    // Numeric panel
    // -----------------------------------------------------------------------
    std::vector<NumericEntry> entries;
    entries.reserve(config_.channel_count);

    {
        std::lock_guard<std::mutex> lock(latest_mutex_);
        for (uint32_t i = 0; i < config_.channel_count; ++i) {
            NumericEntry e;
            e.color = channel_color(i);
            e.value = latest_values_.count(i) ? latest_values_.at(i)
                                              : std::numeric_limits<double>::quiet_NaN();
            switch (i) {
                case 0: e.label = "HR";   e.unit = "bpm";  break;
                case 1: e.label = "SpO2"; e.unit = "%";    break;
                case 2: e.label = "BP";   e.unit = "mmHg"; break;
                case 3: e.label = "RR";   e.unit = "brpm"; break;
                default:
                    e.label = "CH" + std::to_string(i);
                    e.unit  = "";
                    break;
            }
            entries.push_back(std::move(e));
        }
    }
    draw_numeric_panel(layout.numeric_panel, entries);

    // -----------------------------------------------------------------------
    // Status bar
    // -----------------------------------------------------------------------
    draw_status_bar(layout.status_bar, "ICU_SIM_01", "Adult");

    // -----------------------------------------------------------------------
    // Action bar
    // -----------------------------------------------------------------------
    static std::vector<ActionButton> action_buttons = {
        {"Main View",    true},
        {"Menu",         false},
        {"Alarm Limits", false},
        {"Trend Graphs", false},
        {"Profiles",     false},
        {"Patient Data", false},
    };
    int hovered = -1;
    draw_action_bar(layout.action_bar, action_buttons, &hovered);
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

Color TelemetryView::channel_color(uint32_t index) noexcept {
    switch (index % 5) {
        case 0: return WAVEFORM_COLOR_ECG;
        case 1: return WAVEFORM_COLOR_BP;
        case 2: return WAVEFORM_COLOR_CO2;
        case 3: return WAVEFORM_COLOR_RESP;
        default: return WAVEFORM_COLOR_DEFAULT;
    }
}

void TelemetryView::rebuild_panels(Rectangle waveform_area) {
    channel_panels_.clear();
    channel_panels_.reserve(config_.channel_count);

    if (config_.channel_count == 0) return;

    const float panel_h = waveform_area.height /
                          static_cast<float>(config_.channel_count);

    static const char* const kDefaultLabels[] = {
        "I   ECG", "II  BP", "III CO2", "IV  RESP",
        "V   CH4", "VI  CH5", "VII CH6", "VIII CH7",
        "IX  CH8", "X   CH9", "XI  CH10","XII CH11"
    };
    static constexpr size_t kLabelCount =
        sizeof(kDefaultLabels) / sizeof(kDefaultLabels[0]);

    for (uint32_t i = 0; i < config_.channel_count; ++i) {
        ChannelPanel p;
        p.id    = static_cast<ChannelId>(i);
        p.color = channel_color(i);
        p.label = (i < kLabelCount) ? kDefaultLabels[i] : ("CH" + std::to_string(i));
        p.bounds = Rectangle{
            waveform_area.x,
            waveform_area.y + static_cast<float>(i) * panel_h,
            waveform_area.width,
            panel_h
        };
        channel_panels_.push_back(std::move(p));
    }
}
