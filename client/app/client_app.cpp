#include "client_app.h"
#include "../common/logging.h"
#include "../config/render_config.h"
#include <raylib.h>
#include <vector>
#include <numeric>

ClientApp::ClientApp(ClientConfig cfg)
    : config_(cfg)
    , view_(cfg)
    , grpc_client_(cfg.server_address) {}

ClientApp::~ClientApp() {
    grpc_client_.stop();
}

void ClientApp::run() {
    // -----------------------------------------------------------------------
    // Window initialisation
    // -----------------------------------------------------------------------
    SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_WINDOW_RESIZABLE);
    InitWindow(config_.window_width, config_.window_height,
               "ICU Telemetry Monitor");
    SetTargetFPS(config_.target_fps);

    spdlog::info("[client_app] Window opened {}x{}",
                 config_.window_width, config_.window_height);

    // -----------------------------------------------------------------------
    // Start gRPC live stream
    // -----------------------------------------------------------------------
    std::vector<uint32_t> channels(config_.channel_count);
    std::iota(channels.begin(), channels.end(), 0u);

    grpc_client_.start_live_stream(
        channels,
        [this](const telemetry::Packet& pkt) {
            view_.on_packet(pkt);
        });

    spdlog::info("[client_app] Connecting to {} (channels: {})",
                 config_.server_address, config_.channel_count);

    // -----------------------------------------------------------------------
    // Main render loop
    // -----------------------------------------------------------------------
    while (!WindowShouldClose()) {
        BeginDrawing();
        ClearBackground(MONITOR_BG);

        // TelemetryView composes all UI regions in one call
        view_.render(GetScreenWidth(), GetScreenHeight());

        // Connection status overlay (small indicator in top-right corner)
        if (!grpc_client_.is_connected()) {
            const char* msg = "Connecting...";
            const int   sz  = 14;
            const int   w   = MeasureText(msg, sz);
            DrawRectangle(GetScreenWidth() - w - 18, 6, w + 12, sz + 8,
                          Color{60, 10, 10, 200});
            DrawText(msg, GetScreenWidth() - w - 12, 10, sz,
                     Color{255, 80, 80, 255});
        }

        EndDrawing();
    }

    // -----------------------------------------------------------------------
    // Shutdown
    // -----------------------------------------------------------------------
    grpc_client_.stop();
    CloseWindow();
    spdlog::info("[client_app] Window closed, shutdown complete");
}
